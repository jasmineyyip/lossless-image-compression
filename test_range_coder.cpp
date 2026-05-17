#include "range_coder.hpp"
#include <iostream>

int main() {
    std::vector<long> histogram = {3, 5, 2};
    CDF model = build_model(histogram);

    std::vector<uint32_t> original = {0, 1, 2, 1, 0, 2, 1, 1, 0, 2, 2, 0, 1};

    RangeEncoder enc;
    for (uint32_t s : original) enc.encode_symbol(s, model);
    enc.finalize();
    std::cout << "encoded " << original.size() << " symbols into "
              << enc.output.size() << " bytes\n";

    BitReader reader(enc.output);
    RangeDecoder dec(reader);

    bool ok = true;
    for (size_t i = 0; i < original.size(); ++i) {
        uint32_t got = dec.decode_symbol(model);
        if (got != original[i]) {
            std::cout << "MISMATCH at " << i
                      << ": expected " << original[i] << ", got " << got << "\n";
            ok = false;
        }
    }
    std::cout << (ok ? "round-trip OK\n" : "round-trip FAILED\n");
}