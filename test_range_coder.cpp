#include "range_coder.hpp"
#include <iostream>

int main() {
    std::vector<long> histogram = {3, 5, 2};
    CDF model = build_model(histogram);

    std::cout << "total: " << model.total << "\n";
    std::cout << "cdf: ";
    for (uint32_t v : model.table) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}