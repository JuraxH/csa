#include "re2/ca.hh"
#include "re2/glushkov.hh"
#include <cstdint>
#include <string>


using namespace std;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <regex>" << std::endl;
        return 1;
    }
    // stuff to get rid of ^ and $
    string pattern;
    if (argv[1][0] == '^') {
        pattern = string("(?:") +  string((char *)&(argv[1][1]));
    } else {
        pattern = string("\\C*(?:") + string(argv[1]);
    }
    if (pattern.back() == '$') {
        pattern.pop_back();
        pattern = pattern + string(")");
    } else {
        pattern = pattern + string(")\\C*");
    }
    std::cerr << pattern << std::endl;

    auto ca = CA::glushkov::Builder::get_ca(pattern);
    std::cout << ca.to_DOT([] (uint8_t arg) { return std::to_string(arg); }) << std::endl;
}
