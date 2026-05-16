#include "range_coder.hpp"
#include <iostream>

int main() {
    std::vector<long> histogram = {3, 5, 2};
    CDF model = build_model(histogram);

    // std::cout << "total: " << model.total << "\n";
    // std::cout << "cdf: ";
    // for (uint32_t v : model.table) {
    //     std::cout << v << " ";
    // }
    // std::cout << "\n";

    RangeEncoder enc;
    enc.encode_symbol(1, model);

    std::cout << std::hex << std::uppercase
              << "low  = 0x" << enc.low  << "\n"
              << "high  = 0x" << enc.high  << "\n";
}