#include <iostream>

#include "re2/ca.hh"
#include "re2/ca_builder.hh"
#include "re2/glushkov.hh"
#include "util/utf.h"
#include "re2/range_builder.hh"


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
    //auto b = glushkov::Builder::get_ca(argv[1]);
    auto b = glushkov::Builder::get_ca("[©®]");
    cout << b.to_DOT() << "\n"s;
    //auto b = re2::range_builder::Builder();
    //b.prepare(100);
    //b.add_rune_range(0x00, 0x7f);
    //b.add_rune_range(0x80, 0x7ff);
    //b.add_rune_range(0x800, 0xffff);
    //b.add_rune_range(0x100, 0xffff);
    //re2::Rune lo;
    //re2::Rune hi;
    //re2::chartorune(&lo, "¿");
    //re2::chartorune(&hi, "À");
    //b.add_rune_range(lo, lo);
    //b.add_rune_range(hi, hi);
    //b.add_rune_range(0x80, 0x10ffff);
    //cout << b.to_DOT() << "\n"s;

}

//    const int RuneMax[4] = {0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
