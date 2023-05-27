#include <iostream>
//#include "re2/re2.h"
//#include "re2/regexp.h"
#include "re2/ca.hh"
#include "re2/ca_builder.hh"
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
    auto b = Builder(argv[1]);
    auto regexp = b.regexp();
    cout << "original: " << regexp->ToString() << "\n";
    auto regexp_str = b.regexp()->ToString();
    cout << regexp_str << endl;
    for (char &a : regexp_str) {
        cout << a << ", ";
    }
    auto p = b.prog();
    cout << "bytemap_range: " << b.bytemap_range() << endl;
    cout << p->Dump();
    cout << endl;
    regexp = remove_captures(regexp);
    cout << "without captures: " << regexp->ToString() << "\n";
    cout << "is nullable: " << is_nullable(regexp) << "\n";
    regexp = normalize(regexp);
    cout << "normalized: " << regexp->ToString() << "\n";
    auto eq = Equation::get_eq(regexp);
    
    if (regexp->op() == re2::kRegexpCharClass) {
        auto cc = regexp->cc(); 
        print_char_class(cc);
        auto negated_cc = cc->Negate();
        cout << "negated: \n";
        print_char_class(negated_cc);
    }

}
