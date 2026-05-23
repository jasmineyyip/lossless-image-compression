#include <emscripten.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

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
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
uint8_t* compress_image(const uint8_t* input_data, int input_size, int* output_size) {
    int w, h, source_channels;
    if (!stbi_info_from_memory(input_data, input_size, &w, &h, &source_channels))
        return nullptr;
    const int num_channels = (source_channels <= 2) ? 1 : 3;

    unsigned char* data = stbi_load_from_memory(input_data, input_size, &w, &h, nullptr, num_channels);
    if (!data) return nullptr;

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

    // pass 1: Paeth-predict, compute context, accumulate per-context histograms
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
                int idx = y * w + x;
                int a = (x > 0) ? ch[idx - 1] : 0;
                int b = (y > 0) ? ch[idx - w] : 0;
                int cdiag = (x > 0 && y > 0) ? ch[idx - w - 1] : 0;

                int r = ch[idx] - paeth(a, b, cdiag);
                int ctx = compute_context(a, b, cdiag);

                residuals[c][idx] = static_cast<int16_t>(r);
                contexts[c][idx] = static_cast<uint8_t>(ctx);
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

    // pass 2: range-encode, switching CDF by context per pixel
    RangeEncoder enc;
    for (int c = 0; c < num_channels; ++c) {
        for (long i = 0; i < n; ++i) {
            uint32_t sym = static_cast<uint32_t>(residuals[c][i] + RES_OFFSET);
            enc.encode_symbol(sym, models[c][contexts[c][i]]);
        }
    }
    enc.finalize();

    // allocate output: w + h + num_channels + per-channel per-context histograms + encoded bytes
    const long header_size = 4 + 4 + 4 + static_cast<long>(num_channels) * NUM_CONTEXTS * HIST_SIZE * 4;
    const long total_size = header_size + static_cast<long>(enc.output.size());
    uint8_t* result = static_cast<uint8_t*>(malloc(total_size));
    if (!result) return nullptr;

    uint8_t* p = result;
    auto write_u32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };

    write_u32(static_cast<uint32_t>(w));
    write_u32(static_cast<uint32_t>(h));
    write_u32(static_cast<uint32_t>(num_channels));
    for (int c = 0; c < num_channels; ++c)
        for (int k = 0; k < NUM_CONTEXTS; ++k)
            for (long count : hist[c][k])
                write_u32(static_cast<uint32_t>(count));
    memcpy(p, enc.output.data(), enc.output.size());

    *output_size = static_cast<int>(total_size);
    return result;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* decompress_image(const uint8_t* input_data, int input_size,
                          int* out_width, int* out_height, int* out_num_channels) {
    long offset = 0;
    auto read_u32 = [&]() {
        uint32_t v;
        memcpy(&v, input_data + offset, 4);
        offset += 4;
        return v;
    };

    const uint32_t width = read_u32();
    const uint32_t height = read_u32();
    const uint32_t num_channels = read_u32();
    const long n = static_cast<long>(width) * height;

    const int RES_OFFSET = (num_channels == 1) ? 255 : 510;
    const int HIST_SIZE = 2 * RES_OFFSET + 1;

    // read per-channel per-context histograms and build CDFs
    std::vector<std::vector<CDF>> models(num_channels);
    for (uint32_t c = 0; c < num_channels; ++c) {
        models[c].reserve(NUM_CONTEXTS);
        for (int k = 0; k < NUM_CONTEXTS; ++k) {
            std::vector<long> hist(HIST_SIZE);
            for (long& count : hist) count = read_u32();
            models[c].push_back(build_model(hist));
        }
    }

    std::vector<uint8_t> encoded(input_data + offset, input_data + input_size);
    BitReader reader(encoded);
    RangeDecoder dec(reader);

    // decode: channel-outer, pixel-inner; context recomputed from decoded neighbors
    std::vector<std::vector<int>> pixels(num_channels, std::vector<int>(n, 0));

    for (uint32_t c = 0; c < num_channels; ++c) {
        auto& ch = pixels[c];
        for (int y = 0; y < static_cast<int>(height); ++y) {
            for (int x = 0; x < static_cast<int>(width); ++x) {
                int idx = y * static_cast<int>(width) + x;
                int a = (x > 0) ? ch[idx - 1] : 0;
                int b = (y > 0) ? ch[idx - static_cast<int>(width)] : 0;
                int cdiag = (x > 0 && y > 0) ? ch[idx - static_cast<int>(width) - 1] : 0;

                int pred = paeth(a, b, cdiag);
                int ctx = compute_context(a, b, cdiag);
                int residual = static_cast<int>(dec.decode_symbol(models[c][ctx])) - RES_OFFSET;
                ch[idx] = pred + residual;
            }
        }
    }

    // reverse YCoCg-R and pack into interleaved output buffer
    uint8_t* result = static_cast<uint8_t*>(malloc(static_cast<size_t>(n) * num_channels));
    if (!result) return nullptr;

    if (num_channels == 1) {
        for (long i = 0; i < n; ++i)
            result[i] = static_cast<uint8_t>(pixels[0][i]);
    } else {
        for (long i = 0; i < n; ++i) {
            int R, G, B;
            ycocg_r_inverse(pixels[0][i], pixels[1][i], pixels[2][i], R, G, B);
            result[i*3 + 0] = static_cast<uint8_t>(R);
            result[i*3 + 1] = static_cast<uint8_t>(G);
            result[i*3 + 2] = static_cast<uint8_t>(B);
        }
    }

    *out_width = static_cast<int>(width);
    *out_height = static_cast<int>(height);
    *out_num_channels = static_cast<int>(num_channels);
    return result;
}

EMSCRIPTEN_KEEPALIVE
void free_buffer(uint8_t* ptr) {
    free(ptr);
}

}
