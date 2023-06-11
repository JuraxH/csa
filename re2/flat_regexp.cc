#include "flat_regexp.hh"

namespace re2::FlatRegexp {

using std::pair;
using std::make_pair;
using std::unordered_set;
using std::vector;

namespace Rune1 {
    const uint32_t min = 0x0;
    const uint32_t max = 0x7F;
    const uint32_t len = 1;
};

namespace Rune2 {
    const uint32_t min = 0x80;
    const uint32_t max = 0x7FF;
    const uint32_t len = 2;
}

namespace Rune3 {
    const uint32_t min = 0x800;
    const uint32_t max = 0xFFFF;
    const uint32_t len = 3;
}

namespace Rune4 {
    const uint32_t min = 0x10000;
    const uint32_t max = 0x10FFFF;
    const uint32_t len = 4;
}

namespace Char1 {
    const uint8_t min = 0x0;
    const uint8_t max = 0x7F;
}

namespace Charx {
    const uint8_t min = 0x80;
    const uint8_t max = 0xBF;
}

namespace Char2 {
    const uint8_t min = 0xC0;
    const uint8_t max = 0xDF;
}

namespace Char3 {
    const uint8_t min = 0xE0;
    const uint8_t max = 0xEF;
}

namespace Char4 {
    const uint8_t min = 0xF0;
    const uint8_t max = 0xF4;
}

shared_ptr<RegexpNode> FlatRegexp::byte_range_seq_to_node(ByteRangeSeq seq) {
    auto cached = suffix_cache_.find(seq);
    if (cached != suffix_cache_.end()) {
        return cached->second;
    } else {
        unordered_set<ByteType> cur_bytes;
        for (auto i = seq.seq()[0].min(); i <= seq.seq()[0].max(); ++i) {
            cur_bytes.insert(byte_to_symbol(i));
        }

        shared_ptr<RegexpNode> node;
        if (cur_bytes.size() == 1) {
            node = make_shared<RegexpNode>(RegexpNode(Byte(*cur_bytes.begin())));
        } else {
            vector<shared_ptr<RegexpNode>> alters;
            for (auto byte : cur_bytes) {
                alters.push_back(make_shared<RegexpNode>(RegexpNode(Byte(byte))));
            }
            node = make_shared<RegexpNode>(RegexpNode(Alter(std::move(alters))));
        }

        if (seq.seq().size() == 1) {
            suffix_cache_[seq] = node;
            return node;
        } else {
            auto res = make_shared<RegexpNode>(RegexpNode(Concat({
                            node,
                            byte_range_seq_to_node(seq.suffix()),
                            })));
            suffix_cache_[seq] = res;
            return res;
        }
    }
}

shared_ptr<RegexpNode> FlatRegexp::rune_to_bytes(int rune) {
    char chars[re2::UTFmax];
    int len = runetochar(chars, &rune);
    std::vector<ByteType> bytes(len);
    for (auto i = 0; i < len; i++) {
        bytes[i] = byte_to_symbol(chars[i]);
    }
    return make_shared<RegexpNode>(
            RegexpNode(Bytes(std::move(bytes))));
}



void FlatRegexp::bounded_int_seq(vector<shared_ptr<RegexpNode>> &alters,
        int min, int max) {
    char min_str[re2::UTFmax];
    char max_str[re2::UTFmax];
    int len =runetochar(min_str, &min);
    runetochar(max_str, &max);
    uint8_t diff_byte = 0;
    for (; diff_byte < len; ++diff_byte) {
        if (min_str[diff_byte] != max_str[diff_byte]) {
            break;
        }
    }
    int i = 0;
    while (i < len) {
        if (min_str[i] == max_str[i]) {
        }
    }
    if (diff_byte == 0) {
    }
}


void FlatRegexp::add_rune2(vector<shared_ptr<RegexpNode>> &alters, uint32_t min,
        uint32_t max) {
    if (min == Rune2::min && max == Rune2::max) {
        alters.push_back(byte_range_seq_to_node(ByteRangeSeq({
                        ByteRange(Char2::min, Char2::max),
                        ByteRange(Charx::min, Charx::max),
                        })));
        return;
    }
    if (min == max) {
        alters.push_back(rune_to_bytes((int)min));
        return;
    } 
    bounded_int_seq(alters, (int)min, (int)max);
}

void FlatRegexp::add_rune3(vector<shared_ptr<RegexpNode>> &alters, uint32_t min,
        uint32_t max) {
    if (min == Rune3::min && max == Rune3::max) {
        alters.push_back(byte_range_seq_to_node(ByteRangeSeq({
                        ByteRange(Char3::min, Char3::max),
                        ByteRange(Charx::min, Charx::max),
                        ByteRange(Charx::min, Charx::max),
                        })));
        return;
    }
    if (min == max) {
        alters.push_back(rune_to_bytes((int)min));
        return;
    }
}

void FlatRegexp::add_rune4(vector<shared_ptr<RegexpNode>> &alters, uint32_t min,
        uint32_t max) {
    if (min == Rune4::min && max == Rune4::max) {
        alters.push_back(byte_range_seq_to_node(ByteRangeSeq({
                        ByteRange(Char4::min, Char4::max),
                        ByteRange(Charx::min, Charx::max),
                        ByteRange(Charx::min, Charx::max),
                        ByteRange(Charx::min, Charx::max),
                        })));
        return;
    }
    if (min == max) {
        alters.push_back(rune_to_bytes((int)min));
        return;
    }
}

[[nodiscard]] shared_ptr<RegexpNode> FlatRegexp::cc_to_regexp_node(CharClass *cc) {
    std::unordered_set<ByteType> char1;
    vector<shared_ptr<RegexpNode>> alters;
    vector<pair<uint32_t, uint32_t>> atleast2;
    vector<pair<uint32_t, uint32_t>> atleast3;
    // 0x00-0x7F rune1
    for (auto &range : *cc) {
        uint32_t min = *(uint32_t*)(&range.lo);
        uint32_t max = *(uint32_t*)(&range.hi);
        if (max > Rune1::max) { // cutting of the top part now
            max = Rune1::max;
            if (min <= Rune1::max) { // cutting of the bottom part next
                atleast2.push_back(make_pair(Rune2::min, range.hi));
            } else {
                atleast2.push_back(make_pair(range.hi, range.hi));
            }
        }
        for (;min <= max; ++min) {
            char1.insert(byte_to_symbol(static_cast<uint8_t>(min)));
        }
    }
    // 0x80-0x07FF rune2
    for (auto &range : atleast2) {
        uint32_t min = range.first;
        uint32_t max = range.second;
        if (max > Rune2::max) { // cutting of the top part now
            max = Rune2::max;
            if (min <= Rune2::max) { // cutting of the bottom part next
                atleast3.push_back(make_pair(Rune3::min, range.second));
            } else {
                atleast3.push_back(make_pair(range.first, range.second));
            }
        }
        if (min <= max) {
            add_rune2(alters, min, max);
        }
    }
    atleast2.resize(0);
    auto &atleast4 = atleast2;
    // 0x800-0xFFFF rune3
    for (auto &range : atleast3) {
        uint32_t min = range.first;
        uint32_t max = range.second;
        if (max > Rune3::max) {
            max = Rune3::max;
            if (min <= Rune3::max) {
                atleast4.push_back(make_pair(Rune4::min, range.second));
            } else {
                atleast4.push_back(make_pair(range.first, range.second));
            }
        }
        if (min <= max) {
            add_rune3(alters, min, max);
        }
    }
    // 0x10000-0x1FFFFF
    for (auto &range : atleast4) {
        uint32_t min = range.first;
        uint32_t max = range.second;
        if (max > Rune4::max) {
            max = Rune4::max;
        }
        if (min <= max) {
            add_rune4(alters, min, max);
        }
    }
    for (auto byte : char1) {
        alters.push_back(make_shared<RegexpNode>(RegexpNode(Byte(byte))));
    }
    return make_shared<RegexpNode>(RegexpNode(Alter(alters)));
}
    
}
