#include <iostream>
#include "re2/re2.h"
#include "re2/regexp.h"
#include "util/logging.h"


using namespace std;

int main() {
    cout << "sup, bro" << endl;
    RE2::Options options;
    auto regexp = re2::Regexp::Parse("helo*", 
            static_cast<re2::Regexp::ParseFlags>(options.ParseFlags())
            , nullptr);
    cout << regexp->ToString();
}
