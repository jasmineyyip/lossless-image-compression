#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// paeth predictor, return whichever is closest to the linear estimate
static int paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// empirical shannon entropy of a discrete distribution
static double shannon_entropy(const std::vector<long>& counts) {
    long total = 0;
    for (long c : counts) total += c;
    double H = 0.0;
    for (long c : counts) {
        if (c == 0) continue;
        double p = static_cast<double>(c) / total;
        H -= p * std::log2(p);
    }
    return H;
}

static void write_histogram_csv(const std::string& path,
                                int value_offset,
                                const std::vector<long>& hist) {
    std::ofstream out(path);
    out << "value,count\n";
    for (size_t i = 0; i < hist.size(); ++i) {
        out << (static_cast<int>(i) - value_offset) << "," << hist[i] << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image.png>\n";
        return 1;
    }
    const char* path = argv[1];

    int w, h, channels_in_source;
    unsigned char* data = stbi_load(path, &w, &h, &channels_in_source, 1); // force grayscale
    if (!data) {
        std::cerr << "Failed to load image: " << path << "\n";
        std::cerr << "  reason: " << stbi_failure_reason() << "\n";
        return 1;
    }

    const long n = static_cast<long>(w) * h;
    std::cout << "Loaded: " << path << "\n";
    std::cout << "  size: " << w << "x" << h
              << " (" << channels_in_source << " ch in source, converted to grayscale)\n\n";

    // raw pixel histogram
    std::vector<long> raw_hist(256, 0);
    for (long i = 0; i < n; ++i) ++raw_hist[data[i]];

    // residual histogram
    constexpr int RES_OFFSET = 255;
    std::vector<long> res_hist(2 * RES_OFFSET + 1, 0);

    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            int pred;
            if (i == 0 && j == 0) {
                // no neighbors
                pred = 0;
            } else if (i == 0) {
                // left only
                pred = data[j - 1];
            } else if (j == 0) {
                // top only
                pred = data[(i - 1) * w];
            } else {
                int a = data[ i      * w + (j - 1)]; // left
                int b = data[(i - 1) * w +  j     ]; // top
                int c = data[(i - 1) * w + (j - 1)]; // top-left
                pred = paeth(a, b, c);
            }
            int r = static_cast<int>(data[i * w + j]) - pred;
            ++res_hist[r + RES_OFFSET];
        }
    }

    double H_raw = shannon_entropy(raw_hist);
    double H_res = shannon_entropy(res_hist);

    std::cout << "Entropy of raw pixels entropy: " << H_raw << " bits/pixel\n";
    std::cout << "Entropy of residuals: " << H_res << " bits/pixel\n";
    std::cout << "Compression headroom: " << (H_raw - H_res) << " bits/pixel\n";
    std::cout << "Theoretical ratio: " << (H_raw / H_res) << "x\n\n";
    std::cout << "Raw size at 8 bpp: " << n << " bytes\n";
    std::cout << "Lower bound from raw entropy: "
              << static_cast<long>(n * H_raw / 8.0) << " bytes\n";
    std::cout << "Lower bound from residual entropy: "
              << static_cast<long>(n * H_res / 8.0) << " bytes\n";

    write_histogram_csv("raw_histogram.csv",      0,          raw_hist);
    write_histogram_csv("residual_histogram.csv", RES_OFFSET, res_hist);
    std::cout << "\nWrote raw_histogram.csv and residual_histogram.csv\n";

    stbi_image_free(data);
    return 0;
}
