#include "csa.hh"
#include <fstream>
#include <string>
#include <iostream>

using namespace std;

int main(int argc, char **argv) {
    if (argc != 3) {
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[2]);

    if (!input.is_open()) {
        std::cerr << "Failed to open file " << argv[2] << '\n';
        return EXIT_FAILURE;
    }

    CSA::Matcher matcher(argv[1]);
    std::string line;
    unsigned matches = 0;
    while (getline(input, line)) {
        if (matcher.match(line)) {
            ++matches;
        }
    }
    cout << matches << '\n';
}
