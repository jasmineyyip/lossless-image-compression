#include <emscripten.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

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

extern "C" {

// compress a PNG (or any stbi-supported format) from a byte buffer
EMSCRIPTEN_KEEPALIVE
uint8_t* compress_image(const uint8_t* input_data, int input_size, int* output_size) {
    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(input_data, input_size, &w, &h, &channels, 1);
    if (!data) return nullptr;

    const long n = static_cast<long>(w) * h;

    std::vector<int16_t> residuals(n);
    std::vector<long> hist(2 * RES_OFFSET + 1, 0);

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

    CDF model = build_model(hist);

    RangeEncoder enc;
    for (long i = 0; i < n; ++i) {
        uint32_t symbol_index = static_cast<uint32_t>(residuals[i] + RES_OFFSET);
        enc.encode_symbol(symbol_index, model);
    }
    enc.finalize();

    stbi_image_free(data);

    // build output buffer
    const long header_size = 4 + 4 + 511 * 4;
    const long total_size = header_size + static_cast<long>(enc.output.size());
    uint8_t* result = static_cast<uint8_t*>(malloc(total_size));
    if (!result) return nullptr;

    uint8_t* p = result;
    auto write_u32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };

    write_u32(static_cast<uint32_t>(w));
    write_u32(static_cast<uint32_t>(h));
    for (long count : hist) write_u32(static_cast<uint32_t>(count));
    memcpy(p, enc.output.data(), enc.output.size());

    *output_size = static_cast<int>(total_size);
    return result;
}

// decompress a .bin from memory
// returns malloc'd pointer to raw grayscale bytes
EMSCRIPTEN_KEEPALIVE
uint8_t* decompress_image(const uint8_t* input_data, int input_size,
                          int* out_width, int* out_height) {
    long offset = 0;
    auto read_u32 = [&]() {
        uint32_t v;
        memcpy(&v, input_data + offset, 4);
        offset += 4;
        return v;
    };

    uint32_t width  = read_u32();
    uint32_t height = read_u32();

    std::vector<long> hist(2 * RES_OFFSET + 1);
    for (long& count : hist) count = read_u32();

    std::vector<uint8_t> encoded(input_data + offset, input_data + input_size);

    CDF model = build_model(hist);
    BitReader reader(encoded);
    RangeDecoder dec(reader);

    const long n = static_cast<long>(width) * height;
    uint8_t* result = static_cast<uint8_t*>(malloc(n));
    if (!result) return nullptr;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            int idx = y * width + x;
            int a = (x > 0) ? result[idx - 1] : 0;
            int b = (y > 0) ? result[idx - width] : 0;
            int c = (x > 0 && y > 0) ? result[idx - width - 1] : 0;
            int pred = paeth(a, b, c);
            int residual = static_cast<int>(dec.decode_symbol(model)) - RES_OFFSET;
            result[idx] = static_cast<uint8_t>(pred + residual);
        }
    }

    *out_width  = static_cast<int>(width);
    *out_height = static_cast<int>(height);
    return result;
}

EMSCRIPTEN_KEEPALIVE
void free_buffer(uint8_t* ptr) {
    free(ptr);
}

}
