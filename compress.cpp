// compress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include "range_coder.hpp"
#include "ycocg.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

constexpr int NUM_CONTEXTS = 8;
constexpr int CONTEXT_BOUNDARIES[NUM_CONTEXTS - 1] = {4, 12, 28, 60, 124, 252, 1024};

inline int compute_context(int a, int b, int c) {
    int activity = std::abs(a - c) + std::abs(b - c);
    for (int i = 0; i < NUM_CONTEXTS - 1; ++i) {
        if (activity < CONTEXT_BOUNDARIES[i]) return i;
    }
    return NUM_CONTEXTS - 1;
}

static int paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.png> <output.bin>\n";
        return 1;
    }
    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    // detect channel count without decoding the image
    int w, h, source_channels;
    if (!stbi_info(input_path, &w, &h, &source_channels)) {
        std::cerr << "Failed to read image info: " << input_path << "\n";
        std::cerr << "  reason: " << stbi_failure_reason() << "\n";
        return 1;
    }
    const int num_channels = (source_channels <= 2) ? 1 : 3;

    unsigned char* data = stbi_load(input_path, &w, &h, nullptr, num_channels);
    if (!data) {
        std::cerr << "Failed to load image: " << input_path << "\n";
        std::cerr << "  reason: " << stbi_failure_reason() << "\n";
        return 1;
    }

    const long n = static_cast<long>(w) * h;
    const int RES_OFFSET = (num_channels == 1) ? 255 : 510;
    const int HIST_SIZE = 2 * RES_OFFSET + 1;

    // build per-channel int pixel planes, applying YCoCg-R for color
    std::vector<std::vector<int>> pixels(num_channels, std::vector<int>(n));

    if (num_channels == 1) {
        for (long i = 0; i < n; ++i)
            pixels[0][i] = data[i];
    } else {
        for (long i = 0; i < n; ++i) {
            int Y, Co, Cg;
            ycocg_r_forward(data[i*3 + 0], data[i*3 + 1], data[i*3 + 2], Y, Co, Cg);
            pixels[0][i] = Y;
            pixels[1][i] = Co;
            pixels[2][i] = Cg;
        }
    }
    stbi_image_free(data);

    // paeth predict, compute context, accumulate histograms per context
    std::vector<std::vector<int16_t>> residuals(num_channels, std::vector<int16_t>(n));
    std::vector<std::vector<uint8_t>> contexts(num_channels, std::vector<uint8_t>(n));
    std::vector<std::vector<std::vector<long>>> hist(
        num_channels,
        std::vector<std::vector<long>>(NUM_CONTEXTS, std::vector<long>(HIST_SIZE, 0))
    );

    for (int c = 0; c < num_channels; ++c) {
        const auto& ch = pixels[c];
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx   = y * w + x;
                int a = (x > 0) ? ch[idx - 1] : 0;
                int b = (y > 0) ? ch[idx - w] : 0;
                int cdiag = (x > 0 && y > 0) ? ch[idx - w - 1] : 0;

                int r = ch[idx] - paeth(a, b, cdiag);
                int ctx = compute_context(a, b, cdiag);

                residuals[c][idx] = static_cast<int16_t>(r);
                contexts[c][idx]  = static_cast<uint8_t>(ctx);
                hist[c][ctx][r + RES_OFFSET]++;
            }
        }
    }

    // build NUM_CONTEXTS CDFs per channel
    std::vector<std::vector<CDF>> models(num_channels);
    for (int c = 0; c < num_channels; ++c) {
        models[c].reserve(NUM_CONTEXTS);
        for (int k = 0; k < NUM_CONTEXTS; ++k)
            models[c].push_back(build_model(hist[c][k]));
    }

    // range-encode, switching CDF by context per pixel
    RangeEncoder enc;
    for (int c = 0; c < num_channels; ++c) {
        for (long i = 0; i < n; ++i) {
            uint32_t sym = static_cast<uint32_t>(residuals[c][i] + RES_OFFSET);
            enc.encode_symbol(sym, models[c][contexts[c][i]]);
        }
    }
    enc.finalize();

    // write file: width, height, num_channels, per-channel per-context histograms, encoded bytes
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        return 1;
    }
    auto write_u32 = [&](uint32_t v) {
        out.write(reinterpret_cast<const char*>(&v), 4);
    };

    write_u32(static_cast<uint32_t>(w));
    write_u32(static_cast<uint32_t>(h));
    write_u32(static_cast<uint32_t>(num_channels));
    for (int c = 0; c < num_channels; ++c)
        for (int k = 0; k < NUM_CONTEXTS; ++k)
            for (long count : hist[c][k])
                write_u32(static_cast<uint32_t>(count));
    out.write(reinterpret_cast<const char*>(enc.output.data()), enc.output.size());

    // stats
    long file_size = static_cast<long>(out.tellp());
    long encoded_bytes = static_cast<long>(enc.output.size());
    long header_bytes = file_size - encoded_bytes;
    double bpp = (encoded_bytes * 8.0) / (n * num_channels);

    std::cout << "Image: " << w << "x" << h << " (" << n << " pixels, "
              << num_channels << " channel" << (num_channels > 1 ? "s" : "") << ")\n";
    std::cout << "Compressed: " << file_size << " bytes\n";
    std::cout << "Header: " << header_bytes << " bytes\n";
    std::cout << "Encoded: " << encoded_bytes << " bytes (" << bpp << " bits/channel-pixel)\n";
    std::cout << "Raw: " << n * num_channels << " bytes\n";
    std::cout << "Ratio: " << static_cast<double>(n * num_channels) / file_size << "x\n";

    return 0;
}
