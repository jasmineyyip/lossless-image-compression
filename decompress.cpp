// decompress.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "range_coder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

constexpr int RES_OFFSET = 255;

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
    const char* png_path = argv[2];

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

    uint32_t width = read_u32();
    uint32_t height = read_u32();
    const long n = static_cast<long>(width) * height;

    std::vector<long> hist(511);
    for (long& count : hist) count = read_u32();

    std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

    CDF model = build_model(hist);

    BitReader reader(encoded);
    RangeDecoder dec(reader);

    std::vector<uint8_t> pixels(n);
    for (long i = 0; i < n; ++i) {
        int y = i / static_cast<long>(width);
        int x = i % static_cast<long>(width);
        int a = (x > 0) ? pixels[i - 1] : 0;
        int b = (y > 0) ? pixels[i - width] : 0;
        int c = (x > 0 && y > 0) ? pixels[i - width - 1] : 0;

        uint32_t symbol_index = dec.decode_symbol(model);
        int residual = static_cast<int>(symbol_index) - RES_OFFSET;
        pixels[i] = static_cast<uint8_t>(paeth(a, b, c) + residual);
    }

    int w, h, channels;
    unsigned char* original = stbi_load(png_path, &w, &h, &channels, 1);
    if (!original) {
        std::cerr << "can't load " << png_path << ": " << stbi_failure_reason() << "\n";
        return 1;
    }

    if (static_cast<uint32_t>(w) != width || static_cast<uint32_t>(h) != height) {
        std::cerr << "FAIL: size mismatch (" << width << "x" << height
                  << " decoded, " << w << "x" << h << " original)\n";
        stbi_image_free(original);
        return 1;
    }

    if (std::memcmp(pixels.data(), original, static_cast<size_t>(n)) == 0) {
        std::cout << "PASS: " << width << "x" << height << " (" << n << " pixels)\n";
    } else {
        std::cerr << "FAIL: output doesn't match original\n";
        stbi_image_free(original);
        return 1;
    }

    stbi_image_free(original);
    return 0;
}
