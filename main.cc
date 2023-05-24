#include <iostream>
//#include "re2/re2.h"
//#include "re2/regexp.h"
#include "re2/ca.hh"
#include "re2/ca_builder.hh"


using namespace std;
using namespace CA;

void print_char_class(re2::CharClass *cc) {
    cout << "char class:\n";
    for (re2::RuneRange &range : *cc) {
        cout << '\t' << to_string(range.lo) << " - " << to_string(range.hi) << endl;
    }
}

int main() {
    //auto b = Builder("[^©-®]");
    auto b = Builder("aa|bb|cc");
    auto regexp = b.regexp();
    cout << regexp->ToString() << "\n";
    regexp = remove_captures(regexp);
    cout << regexp->ToString() << "\n";
    cout << "is nullable: " << is_nullable(regexp) << "\n";
    regexp = normalize(regexp);
    cout << regexp->ToString() << "\n";
    auto eq = Equation::get_eq(regexp);

#if 0
    auto b = Builder("[^a-b]");
    auto regexp = b.regexp();
    auto cc = regexp->cc(); 
    print_char_class(cc);
    auto negated_cc = cc->Negate();
    print_char_class(negated_cc);

    auto p = b.prog();
    cout << "bytemap_range: " << b.bytemap_range() << endl;
    cout << p->Dump();
    auto regexp_str = b.regexp()->ToString();
    cout << regexp_str << endl;
    for (char &a : regexp_str) {
        cout << a << ", ";
    }
    cout << endl;
#endif

}
