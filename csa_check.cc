#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "re2/ca.hh"
#include "csa.hh"
#include "util/utf.h"
#include "re2/glushkov.hh"


using namespace std;


int main(int argc, char **argv) {
    if (argc < 2) {
        cerr << "missing pattern argument\n";
        return 1;
    }
    CSA::CSA csa(CA::glushkov::Builder::get_ca(argv[1]));
    csa.compute_full();
}

//    const int RuneMax[4] = {0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
