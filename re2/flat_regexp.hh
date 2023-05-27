// provides calls to construct and represent flattened regexp, which is 
// regexp where instead of runes and rune ranges are used bytemap symbols
#include <variant>
#include <vector>
#include <memory>
#include <set>
#include <algorithm>
#include <iostream>
#include <limits>


namespace re2 {

// TODO:
//  anychar -- suport multibyte stuff (maybe more of builder thing)
//
namespace FlatRegexp {

    using std::string;
    using std::to_string;
    using std::vector;
    using std::set;
    using namespace std::string_literals;


    class UniqueByte {
        public:
        UniqueByte(uint8_t byte, uint8_t id) : byte_(byte), id_(id) {}
        string to_str() const {
            if (byte_ >= static_cast<uint8_t>('!') && byte_ <= static_cast<uint8_t>('~')) {
                return string(1, static_cast<char>(byte_)) + "["s + to_string(id_) + "]"s;;
            } else {
                return to_string(byte_) + "["s + to_string(id_) + "]"s;;
            }
        }
        private:
        uint8_t byte_;
        uint8_t id_;
    };

    class UniqueByteFactory {
        public:
        UniqueByte operator()(uint8_t byte) {
            return UniqueByte{byte, cnt_[byte]++};
        }
        private:
        std::array<uint8_t, std::numeric_limits<uint8_t>::max()> cnt_ = {};
    };

    class AsciiChar {
        public:
        AsciiChar(char ch) : ch_(ch) {}
        string to_str() const { return string{ch_}; }
        private:
        char ch_;
    };

    using SymbolType = UniqueByte;
    using SymbolContainer = set<SymbolType>;


    class Symbol {
        public:
        Symbol(SymbolType symbol) : symbol_(symbol) {}
        string to_str() const { return symbol_.to_str(); }
        private:
        SymbolType symbol_;
    };

    class Symbols {
        public:
        Symbols(vector<SymbolType>&& symbols) : symbols_(symbols) {}
        Symbols(vector<SymbolType>& symbols) : symbols_(symbols) {}

        string to_str() const { 
            string str{};
            for (auto const &s : symbols_) {
                str += s.to_str();
            }
            return str;
        }
        private:
        std::vector<SymbolType> symbols_;
    };

    template<typename T>
    class Alter {
        public:
        Alter(vector<T>&& subs) : subs_(subs) {}
        Alter(vector<T>& subs) : subs_(subs) {}
        string to_str() const { 
            string str{};
            auto first = true;
            for (auto const &sub : subs_) {
                if (first) { first = false; } else { str += "|"s; }
                visit([&](auto const &expr) { str += expr.to_str(); }, sub);
            }
            return str;
        }
        private:
        std::vector<T> subs_;
    };

    template<typename T>
    class Concat {
        public:
        Concat(vector<T>&& subs) : subs_(subs) {}
        Concat(vector<T>& subs) : subs_(subs) {}
        string to_str() const { 
            string str{};
            auto first = true;
            for (auto const &sub : subs_) {
                visit([&](auto const &expr) { str += expr.to_str(); }, sub);
            }
            return str;
        }
        private:
        std::vector<T> subs_;
    };

    template<typename T>
    class Repeat {
        public:
        Repeat(T &&sub, int min, int max) : sub_(new T(sub)), min_(min), max_(max) { }

        //Repeat(T &sub, int min, int max) : sub_(new T(sub)), min_(min), max_(max) {}
        string to_str() const { 
            string sub_str = visit([](auto const &expr) -> string
                    { return expr.to_str(); },
                    *sub_);
            return "("s + sub_str + "){"s + to_string(min_) + ","s + to_string(max_) + "}"s;
        }
        private:
        std::shared_ptr<T> sub_;
        int min_;
        int max_;
    };

    // hack to define recursive variant
    template<typename T>
    using Var = std::variant<Symbol, Symbols, Alter<T>, Concat<T>, Repeat<T>>;

    template<template<typename> typename K>
    struct Fix : K<Fix<K>> {
        using K<Fix>::K;
    };


    using Regexp = Fix<Var>;


}

}
