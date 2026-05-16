#pragma once

#include <cstdint>
#include <vector>

struct CDF {
    std::vector<uint32_t> table;
    uint32_t total;

    explicit CDF(const std::vector<uint32_t>& counts) : table(counts.size() + 1, 0), total(0) {
        for (size_t i = 0; i < counts.size(); ++i) {
            table[i] = total;
            total += counts[i];
        }
        table[counts.size()] = total;
    }
};

CDF build_model(const std::vector<long>& histogram) {
    // laplace smoothing
    std::vector<uint32_t> smoothed(histogram.size());
    for (size_t i = 0; i < histogram.size(); ++i) {
        smoothed[i] = static_cast<uint32_t>(histogram[i]) + 1;
    }
    return CDF(smoothed);
}