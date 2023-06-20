#include "re2/ca.hh"
#include "re2/glushkov.hh"
#include <cstdint>
#include <string>


int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <regex>" << std::endl;
        return 1;
    }
    auto ca = CA::glushkov::Builder::get_ca(argv[1]);
    std::cout << ca.to_DOT([] (uint8_t arg) { return std::to_string(arg); }) << std::endl;
}
