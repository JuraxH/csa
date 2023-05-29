// provides calls to construct and represent flattened regexp, which is 
// regexp where instead of runes and rune ranges are used bytemap symbols

#pragma once

#include <variant>
#include <vector>
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <iostream>
#include <limits>

#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/prog.h"


namespace re2 {

// TODO:
//  anychar -- suport multibyte stuff (maybe more of builder thing)
//
namespace FlatRegexp {

    using std::string;
    using std::to_string;
    using std::vector;
    using namespace std::string_literals;


    class UniqueByte {
        public:
        UniqueByte() = default;
        UniqueByte(uint8_t byte, uint8_t id) : byte_(byte), id_(id) {}
        UniqueByte(UniqueByte const &) = default;
        UniqueByte(UniqueByte &&) = default;
        UniqueByte(UniqueByte &) = default;
        UniqueByte& operator=(UniqueByte&&) = default;
        UniqueByte& operator=(UniqueByte&) = default;
        UniqueByte& operator=(UniqueByte const &) = default;
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
            return UniqueByte(byte, static_cast<uint8_t>(cnt_[byte]++));
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

    using SymbolType = uint8_t;
    using SymbolContainer = std::unordered_set<SymbolType>;


    class Epsilon {
        public:
        string to_str() const { return ""s; }
    };

    class Symbol {
        public:
        Symbol(SymbolType symbol) : symbol_(symbol) {}
        string to_str() const { return to_string(symbol_); }
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
                str += to_string(s);
            }
            return str;
        }
        private:
        std::vector<SymbolType> symbols_;
    };

    template<typename T>
    class AlterType {
        public:
        AlterType(vector<T>&& subs) : subs_(subs) {}
        AlterType(vector<T>& subs) : subs_(subs) {}
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
    class ConcatType {
        public:
        ConcatType(vector<T>&& subs) : subs_(subs) {}
        ConcatType(vector<T>& subs) : subs_(subs) {}
        string to_str() const { 
            string str{};
            for (auto const &sub : subs_) {
                visit([&](auto const &expr) { str += expr.to_str(); }, sub);
            }
            return str;
        }
        private:
        std::vector<T> subs_;
    };

    template<typename T>
    class RepeatType {
        public:
        RepeatType(T &&sub, int min, int max) : sub_(new T(sub)), min_(min), max_(max) { }
        RepeatType(T &sub, int min, int max) : sub_(new T(sub)), min_(min), max_(max) { }

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

    template<typename T>
    class PlusType {
        public:
        PlusType(T &&sub) : sub_(new T(sub)) { }
        PlusType(T &sub) : sub_(new T(sub)) { }
        string to_str() const {
            string sub_str = visit([](auto const &expr) -> string
                    { return expr.to_str(); },
                    *sub_);
            return sub_str + "("s + sub_str + "){0,-1}"s;
        }
        private:
        std::shared_ptr<T> sub_;
    };

    // hack to define recursive variant
    template<typename T>
    using Var = std::variant<Epsilon, Symbol, Symbols, AlterType<T>, ConcatType<T>, RepeatType<T>, PlusType<T>>;

    // fixpoint combinator
    template<template<typename> typename K>
    struct Fix : K<Fix<K>> {
        using K<Fix>::K;
    };


    using RegexpNode = Fix<Var>;

    using Concat = ConcatType<RegexpNode>;
    using Alter = AlterType<RegexpNode>;
    using Repeat = RepeatType<RegexpNode>;
    using Plus = PlusType<RegexpNode>;

    class FlatRegexp {
        public:
        FlatRegexp(string const &pattern) : regexp_(nullptr), options_(), 
                prog_(nullptr), bytemap_(nullptr), bytemap_range_(), fact_(),
                top_node_() {
            regexp_ = re2::Regexp::Parse(pattern, 
                    static_cast<re2::Regexp::ParseFlags>(options_.ParseFlags()), 
                    nullptr);
            if (regexp_ == nullptr) {
                throw std::runtime_error("Parsing of regex failed");
            }
            prog_ = regexp_->CompileToProg(options_.max_mem() * 2 / 3);
            if (prog_ == nullptr) {
                throw std::runtime_error("Building of bytemap failed");
            }
            bytemap_range_ = prog_->bytemap_range();
            bytemap_ = prog_->bytemap();
            flatten();
        }
        FlatRegexp(FlatRegexp && other) : regexp_(other.regexp_), options_(other.options_), 
                prog_(other.prog_), bytemap_(other.bytemap_), bytemap_range_(other.bytemap_range_), fact_(other.fact_),
                top_node_(std::move(other.top_node_)) {
            other.bytemap_ = nullptr;
            other.regexp_ = nullptr;
            other.prog_ = nullptr;
        }
        FlatRegexp& operator=(FlatRegexp && other) {
            if (this != std::addressof(other)) {
                if (regexp_ != nullptr) {
                    regexp_->Decref();
                }
                if (prog_ != nullptr) {
                    delete prog_;
                }
                regexp_ = other.regexp_;
                options_ = other.options_;
                prog_ = other.prog_;
                bytemap_ = other.bytemap_;
                bytemap_range_ = other.bytemap_range_;
                fact_ = other.fact_;
                top_node_ = std::move(other.top_node_);
                other.bytemap_ = nullptr;
                other.regexp_ = nullptr;
                other.prog_ = nullptr;
            }
            return *this;
        }

        RegexpNode& top_node() { return top_node_; };
        re2::Regexp *regexp() { return regexp_; }
        re2::Prog *prog() { return prog_; }
        int bytemap_range() { return bytemap_range_; }

        ~FlatRegexp() {
            if (regexp_ != nullptr) {
                regexp_->Decref();
            }
            if (prog_ != nullptr) {
                delete prog_;
            }
        }

        private:

        SymbolType byte_to_symbol(uint8_t byte) {
            return bytemap_[byte];
        }


        vector<SymbolType> runes_to_symbols(Regexp *re) {
            assert(re->op() == re2::kRegexpLiteralString);
            char bytes[UTFmax];
            auto symbols = vector<SymbolType>{};
            symbols.reserve(re->nrunes() * UTFmax);
            for (auto i = 0; i < re->nrunes(); i++) {
                auto len = runetochar(bytes, &(re->runes()[i]));
                for (auto j = 0; j < len; j++) {
                    symbols.push_back(byte_to_symbol(bytes[j]));
                }
            }
            return symbols;
        }

        vector<RegexpNode> flatten_subs(Regexp *re) {
            vector<RegexpNode> subs(re->nsub());
            for (auto i = 0; i < re->nsub(); i++) {
                subs[i] = flatten_node(re->sub()[i]);
            }
            return subs;
        }


        RegexpNode flatten_node(Regexp *re) {
            switch (re->op()) {
                case re2::kRegexpNoMatch:
                case re2::kRegexpEmptyMatch:
                    return Epsilon();
                case re2::kRegexpLiteral: {
                    char bytes[re2::UTFmax];
                    int rune = re->rune();
                    int len = runetochar(bytes, &rune);
                    if (len == 1) {
                        return Symbol(byte_to_symbol(bytes[0]));
                    } else {
                        std::vector<SymbolType> symbols(len);
                        for (auto i = 0; i < len; i++) {
                            symbols[i] = byte_to_symbol(bytes[i]);
                        }
                        return Symbols(std::move(symbols));
                    }
                }
                case re2::kRegexpLiteralString:
                    return Symbols(std::move(runes_to_symbols(re)));
                case re2::kRegexpConcat:
                    return Concat(flatten_subs(re));
                case re2::kRegexpAlternate:
                    return Alter(flatten_subs(re));
                case re2::kRegexpStar:
                    return Repeat(flatten_node(re->sub()[0]), 0, -1);
                case re2::kRegexpRepeat:
                    return Repeat(flatten_node(re->sub()[0]), re->min(), re->max());
                case re2::kRegexpPlus:
                    return Plus(flatten_node(re->sub()[0])); 
                case re2::kRegexpQuest:
                    return Alter({flatten_node(re->sub()[0]), Epsilon()}); 
                case re2::kRegexpAnyChar: // TODO: support for multibyte symbols
                case re2::kRegexpAnyByte:
                    return Symbol(bytemap_range_ + 1);
                case re2::kRegexpCharClass: { // TODO: support for runes larger than 128
                    std::unordered_set<SymbolType> alters;
                    auto cc = re->cc();
                    for (auto range = cc->begin(); range != cc->end(); ++range) {
                        assert(range->lo <= 128 && range->hi <= 128); // only ascii ranges for now
                        for (auto i = range->lo; i <= range->hi; i++) {
                            alters.insert(byte_to_symbol(i));
                        }
                    }
                    if (alters.size() == 1) {
                        return Symbol(*alters.begin());
                    } else {
                        vector<RegexpNode> symbols;
                        for (auto const &alter : alters) {
                            symbols.push_back(Symbol(alter));
                        }
                        return Alter(symbols);
                    }
                }
                case re2::kRegexpCapture:
                    return flatten_node(re->sub()[0]);
                case re2::kRegexpBeginLine:
                case re2::kRegexpEndLine:
                case re2::kRegexpWordBoundary:
                case re2::kRegexpNoWordBoundary:
                case re2::kRegexpBeginText:
                case re2::kRegexpEndText:
                default:
                    throw std::runtime_error("Use of unimplemented RegexpOp: "s
                            + std::to_string(re->op()) + " in get_eq()");
            }
        }

        void flatten() {
            top_node_ = flatten_node(regexp_);
        }

        re2::Regexp *regexp_;
        RE2::Options options_;
        re2::Prog *prog_;
        uint8_t *bytemap_;
        int bytemap_range_;
        UniqueByteFactory fact_;
        RegexpNode top_node_;
    };

}

}
