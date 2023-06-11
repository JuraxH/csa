#include <iostream>

#include "re2/ca.hh"
#include "re2/ca_builder.hh"
#include "re2/glushkov.hh"
#include "util/utf.h"


using namespace std;
using namespace CA;

void print_char_class(re2::CharClass *cc) {
    cout << "char class:\n";
    for (re2::RuneRange &range : *cc) {
        char lo[5]; 
        char hi[5]; 
        lo[re2::runetochar(lo, &range.lo)] = '\0';
        hi[re2::runetochar(hi, &range.hi)] = '\0';
        cout << '\t' << to_string(range.lo) << ": " << lo
            << " - " << to_string(range.hi) << ": " << hi << endl;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cerr << "missing pattern argument\n";
        return 1;
    }
    //auto b = Builder("[^©-®]");
    auto b = glushkov::Builder::get_ca(argv[1]);
    cout << b.to_DOT() << "\n"s;
}
