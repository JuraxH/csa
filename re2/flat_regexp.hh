// provides calls to construct and represent flattened regexp, which is 
// regexp where instead of runes and rune ranges are used bytemap bytes

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



// TODO:
//  anychar -- suport multibyte stuff (maybe more of builder thing)
//
namespace re2::FlatRegexp {

    using namespace std::string_literals;
    using std::string;
    using std::to_string;
    using std::vector;
    using std::shared_ptr;
    using std::make_shared;

    using ByteType = uint8_t;


    class Epsilon {
        public:
        string to_str() const { return ""s; }
    };

    class Byte {
        public:
        Byte(ByteType byte) : byte_(byte) {}
        
        ByteType byte() const { return byte_; }
        string to_str() const { return to_string(byte_); }

        private:
        ByteType byte_;
    };

    class Bytes {
        public:
        Bytes(vector<ByteType>&& bytes) : bytes_(std::move(bytes)) {}
        Bytes(vector<ByteType> const& bytes) : bytes_(bytes) {}

        vector<ByteType> const &bytes() const { return bytes_; }

        string to_str() const { 
            string str{};
            for (auto const &s : bytes_) {
                str += to_string(s);
            }
            return str;
        }

        private:
        std::vector<ByteType> bytes_;
    };

    template<typename T>
    class AlterType {
        public:
        AlterType(vector<shared_ptr<T>>&& subs) : subs_(std::move(subs)) {}
        AlterType(vector<shared_ptr<T>>& subs) : subs_(subs) {}

        vector<shared_ptr<T>> const &subs() const { return subs_; }

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
        std::vector<shared_ptr<T>> subs_;
    };

    template<typename T>
    class ConcatType {
        public:
        ConcatType(vector<shared_ptr<T>>&& subs) : subs_(std::move(subs)) {}
        ConcatType(vector<shared_ptr<T>>& subs) : subs_(subs) {}

        vector<shared_ptr<T>> const &subs() const { return subs_; }

        string to_str() const { 
            string str{};
            for (auto const &sub : subs_) {
                visit([&](auto const &expr) { str += expr.to_str(); }, *sub);
            }
            return str;
        }

        private:
        std::vector<shared_ptr<T>> subs_;
    };

    template<typename T>
    class RepeatType {
        public:
        RepeatType(T &&sub, int min, int max) : sub_(make_shared<T>(std::move(sub))), min_(min), max_(max) { }
        RepeatType(T &sub, int min, int max) : sub_(make_shared(sub)), min_(min), max_(max) { }
        RepeatType(T const &sub, int min, int max) : sub_(make_shared(sub)), min_(min), max_(max) { }
        RepeatType(shared_ptr<T> const &sub, int min, int max) : sub_(sub), min_(min), max_(max) { }

        shared_ptr<T> const &sub() const { return sub_; }
        int min() const { return min_; }
        int max() const { return max_; }

        string to_str() const { 
            string sub_str = visit([](auto const &expr) -> string
                    { return expr.to_str(); },
                    *sub_);
            return "("s + sub_str + "){"s + to_string(min_) 
                + ","s + to_string(max_) + "}"s;
        }

        private:
        std::shared_ptr<T> sub_; // could probably be unique, i could not compile it
        int min_;
        int max_;
    };

    // could be replaced with R(R){0,-1}, but that is done latter, to save memory
    template<typename T>
    class PlusType {
        public:
        PlusType(T &&sub) : sub_(make_shared<T>(std::move(sub))) { }
        PlusType(T &sub) : sub_(make_shared(sub)) { }
        PlusType(T const &sub) : sub_(make_shared(sub)) { }
        PlusType(shared_ptr<T> const &sub) : sub_(sub) { }

        shared_ptr<T> const &sub() const { return sub_; }

        string to_str() const {
            string sub_str = visit([](auto const &expr) -> string
                    { return expr.to_str(); },
                    *sub_);
            return sub_str + "("s + sub_str + ")*"s;
        }

        private:
        std::shared_ptr<T> sub_;
    };

    // definition of recursive variant
    template<typename T>
    using Var = std::variant<Epsilon, Byte, Bytes, AlterType<T>, ConcatType<T>, RepeatType<T>, PlusType<T>>;

    // fixpoint combinator
    template<template<typename> typename K>
    struct Fix : K<Fix<K>> {
        using K<Fix>::K;
    };

    // fixed definition of variant
    using RegexpNode = Fix<Var>;

    using Concat = ConcatType<RegexpNode>;
    using Alter = AlterType<RegexpNode>;
    using Repeat = RepeatType<RegexpNode>;
    using Plus = PlusType<RegexpNode>;

    class FlatRegexp {
        public:
        FlatRegexp(string const &pattern) : regexp_(nullptr), options_(), 
        prog_(nullptr), bytemap_(nullptr), bytemap_range_(), top_node_() {
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
                prog_(other.prog_), bytemap_(other.bytemap_), bytemap_range_(other.bytemap_range_),
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
                top_node_ = std::move(other.top_node_);
                other.bytemap_ = nullptr;
                other.regexp_ = nullptr;
                other.prog_ = nullptr;
            }
            return *this;
        }

        ~FlatRegexp() {
            if (regexp_ != nullptr) {
                regexp_->Decref();
            }
            if (prog_ != nullptr) {
                delete prog_;
            }
        }

        [[nodiscard]] RegexpNode& top_node() { return top_node_; };
        [[nodiscard]] re2::Regexp *regexp() { return regexp_; }
        [[nodiscard]] re2::Prog *prog() { return prog_; }
        [[nodiscard]] int bytemap_range() const { return bytemap_range_; }
        [[nodiscard]] uint8_t const* bytemap() const { return bytemap_; }

        private:
        [[nodiscard]] ByteType byte_to_symbol(uint8_t byte) {
            return bytemap_[byte];
        }

        [[nodiscard]] vector<ByteType> runes_to_symbols(Regexp *re) {
            assert(re->op() == re2::kRegexpLiteralString);
            char chars[UTFmax];
            auto bytes = vector<ByteType>{};
            bytes.reserve(re->nrunes() * UTFmax);
            for (auto i = 0; i < re->nrunes(); i++) {
                auto len = runetochar(chars, &(re->runes()[i]));
                for (auto j = 0; j < len; j++) {
                    bytes.push_back(byte_to_symbol(chars[j]));
                }
            }
            return bytes;
        }

        [[nodiscard]] vector<shared_ptr<RegexpNode>> flatten_subs(Regexp *re) {
            vector<shared_ptr<RegexpNode>> subs(re->nsub());
            for (auto i = 0; i < re->nsub(); i++) {
                subs[i] = make_shared<RegexpNode>(RegexpNode(flatten_node(re->sub()[i])));
            }
            return subs;
        }

        [[nodiscard]] RegexpNode flatten_node(Regexp *re) {
            switch (re->op()) {
                case re2::kRegexpNoMatch:
                case re2::kRegexpEmptyMatch:
                    return Epsilon();
                case re2::kRegexpLiteral: {
                    char chars[re2::UTFmax];
                    int rune = re->rune();
                    int len = runetochar(chars, &rune);
                    if (len == 1) {
                        return Byte(byte_to_symbol(chars[0]));
                    } else {
                        std::vector<ByteType> bytes(len);
                        for (auto i = 0; i < len; i++) {
                            bytes[i] = byte_to_symbol(chars[i]);
                        }
                        return Bytes(std::move(bytes));
                    }
                }
                case re2::kRegexpLiteralString:
                    return Bytes(runes_to_symbols(re));
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
                    return Alter({make_shared<RegexpNode>(RegexpNode(flatten_node(re->sub()[0]))),
                            make_shared<RegexpNode>(RegexpNode(Epsilon()))}); 
                case re2::kRegexpAnyChar: // TODO: support for multibyte bytes
                case re2::kRegexpAnyByte:
                    return Byte(bytemap_range_);
                case re2::kRegexpCharClass: { // TODO: support for runes larger than 128
                    std::unordered_set<ByteType> alters;
                    auto cc = re->cc();
                    for (auto range = cc->begin(); range != cc->end(); ++range) {
                        assert(range->lo <= 128 && range->hi <= 128); // only ascii ranges for now
                        for (auto i = range->lo; i <= range->hi; i++) {
                            alters.insert(byte_to_symbol(i));
                        }
                    }
                    if (alters.size() == 1) {
                        return Byte(*alters.begin());
                    } else {
                        vector<shared_ptr<RegexpNode>> bytes;
                        for (auto const &alter : alters) {
                            bytes.push_back(make_shared<RegexpNode>(RegexpNode(Byte(alter))));
                        }
                        return Alter(bytes);
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
        RegexpNode top_node_;
    };

}

