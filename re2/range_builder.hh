#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cassert>
#include <string>
#include <sstream>
#include <iostream>

#include "re2/regexp.h"

// the algorithm is stolen from re2/compile.cc , it is sligtly
// modified to be used to construct CA
namespace re2::range_builder {

    inline std::string to_hex(uint8_t byte) {
        static char hex_digits[] = "0123456789ABCDEF";
        std::string str = "0x";
        str += hex_digits[byte >> 4];
        str += hex_digits[byte & 0xF];
        return str;
    }

    struct RangeState {
        std::vector<uint32_t> next;
        uint8_t lo;
        uint8_t hi;
    };
    
    using RangeId = uint32_t;
    
    const RangeId RangeSeqEnd = 0;

    const int RuneMax[4] = {0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

    inline uint64_t byte_range_cache_key(uint8_t lo, uint8_t hi, RangeId next) {
        return static_cast<uint64_t>(lo) 
            | static_cast<uint64_t>(hi) << 8 
            | static_cast<uint64_t>(next) << 16;
    }

    class Builder {
        public:
        Builder() : ranges_(1, {{}, 0, 0}), trie_root_(), byte_range_cache_(), prev_(0) {};

        std::vector<RangeState> const& ranges() const { return ranges_; }
        std::vector<RangeId> const& root() const { return trie_root_; }
        
        // true if the ptr has the same value as the currenty built automaton
        bool prepare(uint64_t ptr) {
            if (ptr == prev_) {
                return true;
            }
            ranges_.resize(1);
            trie_root_.clear();
            byte_range_cache_.clear();
            prev_ = ptr;
            return false;
        }

        void add_rune_range(int lo, int hi) {
            if (lo > hi) {
                return;
            }

            if (lo == 0x80 && hi == 0x10FFFF) {
                add_0x80_0x10ffff();
                return;
            }

            for (auto i = 0; i < re2::UTFmax - 1; i++) {
                if (lo <= RuneMax[i] && hi > RuneMax[i]) {
                    add_rune_range(lo, RuneMax[i]);
                    add_rune_range(RuneMax[i] + 1, hi);
                    return;
                }
            }

            if (hi <= RuneMax[0]) {
                trie_root_.push_back(no_cache_range(lo, hi, RangeSeqEnd));
                return;
            }
            // from now on all runes are at least 2 bytes long

            // clipping the ranges
            for (int i = 1; i < re2::UTFmax; i++) {
                uint32_t mask = (1<<(6*i)) - 1;
                if ((lo & ~mask) != (hi & ~mask)) {
                    if ((lo & mask) != 0) {
                        add_rune_range(lo, (lo | mask));
                        add_rune_range((lo | mask) + 1, hi);
                        return;
                    }
                    if ((hi & mask) != mask) {
                        add_rune_range(lo, (hi & ~mask) - 1);
                        add_rune_range((hi & ~mask), hi);
                        return;
                    }
                }
            }

            // now the range is clipped and can be treated as intervals of bytes
            char min[re2::UTFmax];
            char max[re2::UTFmax];
            int len = re2::runetochar(min, &lo);
            int len2 = re2::runetochar(max, &hi);
            assert(len == len2);

            // zero means that the range is last
            int id = 0;
            for (int i = len - 1; i >= 0; --i) {
                if (i == len - 1 || (min[i] != max[i] && i != 0)) {
                    id = cache_range(min[i], max[i], id);
                } else {
                    id = no_cache_range(min[i], max[i], id);
                }
            }
            add_to_root(id);
        }

        std::string to_DOT() const {
            std::string res = "digraph {\n";
            std::vector<bool> discovered(ranges_.size(), false);
            std::vector<RangeId> stack;
            for (auto i : trie_root_) {
                res += std::to_string(i) + " [label=\"" 
                    + to_hex(ranges_[i].lo) + "-" 
                    + to_hex(ranges_[i].hi) + "\"];\n";
                for (size_t j = 0; j < ranges_[i].next.size(); j++) {
                    res += std::to_string(i) + " -> " + std::to_string(ranges_[i].next[j]) + ";\n";
                    if (!discovered[ranges_[i].next[j]]) {
                        stack.push_back(ranges_[i].next[j]);
                        discovered[ranges_[i].next[j]] = true;
                    }
                }
            }
            while (!stack.empty()) {
                auto id = stack.back();
                stack.pop_back();
                if (id == RangeSeqEnd) {
                    res += std::to_string(id) + " [label=\"end\"];\n";
                    continue;
                }
                res += std::to_string(id) + " [label=\"" 
                    + to_hex(ranges_[id].lo) + "-" 
                    + to_hex(ranges_[id].hi) + "\"];\n";
                for (size_t j = 0; j < ranges_[id].next.size(); j++) {
                    res += std::to_string(id) + " -> " + std::to_string(ranges_[id].next[j]) + ";\n";
                    if (!discovered[ranges_[id].next[j]]) {
                        stack.push_back(ranges_[id].next[j]);
                        discovered[ranges_[id].next[j]] = true;
                    }
                }
            }
            res += "}";
            return res;
        }
        private:

        void add_0x80_0x10ffff() {
            int suffix = no_cache_range(0x80, 0xBF, RangeSeqEnd);
            trie_root_.push_back(no_cache_range(0xC2, 0xDF, suffix));

            suffix = no_cache_range(0x80, 0xBF, suffix);
            trie_root_.push_back(no_cache_range(0xE0, 0xEF, suffix));

            suffix = no_cache_range(0x80, 0xBF, suffix);
            trie_root_.push_back(no_cache_range(0xF0, 0xF4, suffix));
        }

        bool are_ranges_eq(RangeId r1, RangeId r2) {
            return ranges_[r1].lo == ranges_[r2].lo &&
                   ranges_[r1].hi == ranges_[r2].hi;
        }

        bool is_cached(RangeId range) {
            if (ranges_[range].next.size() > 1) {
                return false;
            } else {
                auto key = byte_range_cache_key(
                    ranges_[range].lo,
                    ranges_[range].hi,
                    ranges_[range].next[0]
                );
                if (byte_range_cache_.find(key) != byte_range_cache_.end()) {
                    return true;
                } else {
                    return false;
                }
            }
        }

        RangeId no_cache_range(uint8_t lo, uint8_t hi, RangeId next) {
            ranges_.push_back(RangeState{{next}, lo, hi});
            return ranges_.size() - 1;
        }

        RangeId cache_range(uint8_t lo, uint8_t hi, RangeId next) {
            uint64_t key = byte_range_cache_key(lo, hi, next);
            auto it = byte_range_cache_.find(key);
            if (it != byte_range_cache_.end()) {
                return it->second;
            }
            auto i = no_cache_range(lo, hi, next);
            byte_range_cache_[key] = i;
            return i;
        }

        void add_range_to_node(RangeId root, RangeId id) {
            if (is_cached(root)) {
                ranges_.push_back(RangeState{{ranges_[root].next[0]}, 
                        ranges_[root].lo, ranges_[root].hi});
                root = ranges_.size() - 1;
            }
            if (are_ranges_eq(ranges_[root].next.back(), id)) {
                add_range_to_node(ranges_[root].next.back(), ranges_[id].next[0]);
                return;
            }
            ranges_[root].next.push_back(id);
        }

        void add_to_root(RangeId id) {
            if (trie_root_.empty()) {
                trie_root_.push_back(id);
            } else {
                RangeId root = trie_root_.back();
                if(are_ranges_eq(root, id)) {
                    RangeId next = ranges_[id].next[0];
                    ranges_.pop_back();
                    add_range_to_node(root, next);
                } else {
                    trie_root_.push_back(id);
                }
            }
        }


        std::vector<RangeState> ranges_;
        std::vector<RangeId> trie_root_;
        std::unordered_map<uint64_t, RangeId> byte_range_cache_;
        uint64_t prev_;
    };
}
