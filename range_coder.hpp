#pragma once

#include <cstdint>
#include <vector>

// cumulative distribution function
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

struct RangeEncoder {
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFF;
    std::vector<uint8_t> output;

    void encode_symbol(uint32_t symbol_index, const CDF& model) {
        uint32_t cdf_low = model.table[symbol_index];
        uint32_t cdf_high = model.table[symbol_index + 1];
        uint32_t total = model.total;

        uint64_t range = static_cast<uint64_t>(high - low) + 1;

        uint32_t new_high = static_cast<uint32_t>(low + (range * cdf_high) / total - 1);
        uint32_t new_low = static_cast<uint32_t>(low + (range * cdf_low) / total);

        high = new_high;
        low  = new_low;

        renormalize();
    }

    void renormalize() {
        while ((low ^ high) >> 24 == 0) {
            output.push_back(static_cast<uint8_t>(low >> 24));
            low <<= 8;
            high = (high << 8) | 0xFF;
        }
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