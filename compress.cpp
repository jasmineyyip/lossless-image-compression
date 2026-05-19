// compress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include "range_coder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

constexpr int RES_OFFSET = 255;

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

    int w, h, channels_in_source;
    unsigned char* data = stbi_load(input_path, &w, &h, &channels_in_source, 1);
    if (!data) {
        std::cerr << "Failed to load image: " << input_path << "\n";
        std::cerr << "  reason: " << stbi_failure_reason() << "\n";
        return 1;
    }

    const long n = static_cast<long>(w) * h;

    // compute residuals and histogram
    std::vector<int16_t> residuals(n);
    std::vector<long> hist(2 * RES_OFFSET + 1, 0);  // size 511

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int a = (x > 0) ? data[idx - 1] : 0;
            int b = (y > 0) ? data[idx - w] : 0;
            int c = (x > 0 && y > 0) ? data[idx - w - 1] : 0;
            int r = static_cast<int>(data[idx]) - paeth(a, b, c);
            residuals[idx] = static_cast<int16_t>(r);
            hist[r + RES_OFFSET]++;
        }
    }

    // build CDF from histogram
    CDF model = build_model(hist);

    // range-encode all residuals
    RangeEncoder enc;
    for (long i = 0; i < n; ++i) {
        uint32_t symbol_index = static_cast<uint32_t>(residuals[i] + RES_OFFSET);
        enc.encode_symbol(symbol_index, model);
    }
    enc.finalize();

    // write file (width, height, histogram, then encoded bytes)
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
    for (long count : hist) {
        write_u32(static_cast<uint32_t>(count));
    }
    out.write(reinterpret_cast<const char*>(enc.output.data()), enc.output.size());

    // stats
    long file_size = static_cast<long>(out.tellp());
    long encoded_bytes = static_cast<long>(enc.output.size());
    double bpp = (encoded_bytes * 8.0) / n;

    std::cout << "Image: " << w << "x" << h << " (" << n << " pixels)\n";
    std::cout << "Compressed: " << file_size << " bytes\n";
    std::cout << "Header: " << (file_size - encoded_bytes) << " bytes\n";
    std::cout << "Encoded: " << encoded_bytes << " bytes (" << bpp << " bits/pixel)\n";
    std::cout << "Raw 8 bpp: " << n << " bytes\n";
    std::cout << "Ratio: " << static_cast<double>(n) / file_size << "x\n";

    stbi_image_free(data);
    return 0;
}