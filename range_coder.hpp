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
    
    uint32_t bit_buffer = 0;
    int bits_in_buffer = 0;
    uint32_t pending = 0;

    void emit_bit(uint32_t bit) {
        auto push = [&](uint32_t b) {
            bit_buffer = (bit_buffer << 1) | (b & 1);
            if (++bits_in_buffer == 8) {
                output.push_back(bit_buffer);
                bit_buffer = 0;
                bits_in_buffer = 0;
            }
        };
        push(bit);
        for (uint32_t i = 0; i < pending; ++i) {
            push(1 - bit);
        }
        pending = 0;
    }

    void renormalize() {
        while (true) {
            if (high < 0x80000000) {
                emit_bit(0);
            } else if (low >= 0x80000000) {
                emit_bit(1);
                low  -= 0x80000000;
                high -= 0x80000000;
            } else if (low >= 0x40000000 && high < 0xC0000000) {
                pending++;
                low  -= 0x40000000;
                high -= 0x40000000;
            } else {
                break;
            }
            low <<= 1;
            high = (high << 1) | 1;
        }
    }

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

    void finalize() {
        pending++;
        if (low < 0x40000000) {
            emit_bit(0);
        } else {
            emit_bit(1);
        }
        while (bits_in_buffer != 0) {
            emit_bit(0);
        }
    }
};

struct BitReader{
    const std::vector<uint8_t>& input;
    size_t byte_pos = 0;
    uint32_t bit_buffer = 0;
    int bits_in_buffer = 0;

    explicit BitReader(const std::vector<uint8_t>& data) : input(data) {}

    uint32_t read_bit() {
        if (bits_in_buffer == 0) {
            bit_buffer = (byte_pos < input.size()) ? input[byte_pos++] : 0;
            bits_in_buffer = 8;
        }
        return (bit_buffer >> --bits_in_buffer) & 1;
    }
};

struct RangeDecoder {
    uint32_t low = 0;
    uint32_t high = 0xFFFFFFFF;
    uint32_t code = 0;
    BitReader& reader;

    explicit RangeDecoder(BitReader& r) : reader(r) {
        for (int i = 0; i < 32; ++i) {
            code = (code << 1) | reader.read_bit();
        }
    }

    void renormalize() {
        while (true) {
            if (high < 0x80000000) {
                // top bit determined as 0, no subtraction needed
            } else if (low >= 0x80000000) {
                code -= 0x80000000;
                low  -= 0x80000000;
                high -= 0x80000000;
            } else if (low >= 0x40000000 && high < 0xC0000000) {
                code -= 0x40000000;
                low  -= 0x40000000;
                high -= 0x40000000;
            } else {
                break;
            }
            low  <<= 1;
            high = (high << 1) | 1;
            code = (code << 1) | reader.read_bit();
        }
    }

    uint32_t decode_symbol(const CDF& model) {
        uint64_t range = static_cast<uint64_t>(high - low) + 1;
        uint64_t scaled = ((static_cast<uint64_t>(code - low) + 1) * model.total - 1) / range;

        // find symbol index using binary search
        size_t symbol_index = 0;
        size_t left = 0, right = model.table.size() - 1;
        while (left < right) {
            size_t mid = (left + right) / 2;
            if (model.table[mid] > scaled) {
                right = mid;
            } else {
                left = mid + 1;
            }
        }
        symbol_index = left - 1;

        uint32_t cdf_low = model.table[symbol_index];
        uint32_t cdf_high = model.table[symbol_index + 1];

        high = low + static_cast<uint32_t>((range * cdf_high) / model.total - 1);
        low  = low + static_cast<uint32_t>((range * cdf_low) / model.total);

        renormalize();
        return symbol_index;
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