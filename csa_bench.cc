#include "re2/csa.hh"
#include <fstream>
#include <string>
#include <iostream>

using namespace std;

int main(int argc, char **argv) {
    if (argc != 3) {
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[2]);

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

    CSA::CSA csa{pattern};
    std::string line;
    unsigned matches = 0;
    while (getline(input, line)) {
        if (csa.match(line)) {
            ++matches;
        }
    }
    cout << matches << '\n';
}
