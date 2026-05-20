// decompress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "range_coder.hpp"
#include "ycocg.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <input.bin> <original.png>\n";
        return 1;
    }
    const char* bin_path = argv[1];
    const char* original_path = argv[2];

    std::ifstream in(bin_path, std::ios::binary);
    if (!in) {
        std::cerr << "can't open " << bin_path << "\n";
        return 1;
    }

    auto read_u32 = [&]() {
        uint32_t v;
        in.read(reinterpret_cast<char*>(&v), 4);
        return v;
    };

    const uint32_t width = read_u32();
    const uint32_t height = read_u32();
    const uint32_t num_channels = read_u32();
    const long n = static_cast<long>(width) * height;

    const int RES_OFFSET = (num_channels == 1) ? 255 : 510;
    const int HIST_SIZE = 2 * RES_OFFSET + 1;

    // read per-channel histograms and build CDFs
    std::vector<CDF> models;
    models.reserve(num_channels);
    for (uint32_t c = 0; c < num_channels; ++c) {
        std::vector<long> hist(HIST_SIZE);
        for (long& count : hist) count = read_u32();
        models.push_back(build_model(hist));
    }

    // slurp remaining bytes for the range decoder
    std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

    BitReader reader(encoded);
    RangeDecoder dec(reader);

    // decode: channel-outer, pixel-inner; reconstruct Paeth in-place
    std::vector<std::vector<int>> pixels(num_channels, std::vector<int>(n, 0));

    for (uint32_t c = 0; c < num_channels; ++c) {
        auto& ch = pixels[c];
        for (int y = 0; y < static_cast<int>(height); ++y) {
            for (int x = 0; x < static_cast<int>(width); ++x) {
                int idx = y * static_cast<int>(width) + x;
                int a = (x > 0) ? ch[idx - 1] : 0;
                int b = (y > 0) ? ch[idx - static_cast<int>(width)] : 0;
                int cdiag = (x > 0 && y > 0) ? ch[idx - static_cast<int>(width) - 1] : 0;

                uint32_t sym = dec.decode_symbol(models[c]);
                int residual = static_cast<int>(sym) - RES_OFFSET;
                ch[idx] = paeth(a, b, cdiag) + residual;
            }
        }
    }

    // reverse YCoCg-R and pack into a flat byte buffer
    std::vector<uint8_t> reconstructed(static_cast<size_t>(n) * num_channels);

    if (num_channels == 1) {
        for (long i = 0; i < n; ++i)
            reconstructed[i] = static_cast<uint8_t>(pixels[0][i]);
    } else {
        for (long i = 0; i < n; ++i) {
            int R, G, B;
            ycocg_r_inverse(pixels[0][i], pixels[1][i], pixels[2][i], R, G, B);
            reconstructed[i*3 + 0] = static_cast<uint8_t>(R);
            reconstructed[i*3 + 1] = static_cast<uint8_t>(G);
            reconstructed[i*3 + 2] = static_cast<uint8_t>(B);
        }
    }

    // load original with the same channel count compress used
    int orig_w, orig_h;
    unsigned char* original = stbi_load(original_path, &orig_w, &orig_h, nullptr, num_channels);
    if (!original) {
        std::cerr << "can't load " << original_path << ": " << stbi_failure_reason() << "\n";
        return 1;
    }

    if (static_cast<uint32_t>(orig_w) != width || static_cast<uint32_t>(orig_h) != height) {
        std::cerr << "FAIL: size mismatch (" << width << "x" << height
                  << " decoded, " << orig_w << "x" << orig_h << " original)\n";
        stbi_image_free(original);
        return 1;
    }

    if (std::memcmp(reconstructed.data(), original, static_cast<size_t>(n) * num_channels) == 0) {
        std::cout << "PASS: " << width << "x" << height << " (" << n << " pixels, "
                  << num_channels << " channel" << (num_channels > 1 ? "s" : "") << ")\n";
    } else {
        std::cerr << "FAIL: output doesn't match original\n";
        stbi_image_free(original);
        return 1;
    }

    stbi_image_free(original);
    return 0;
}
