#include "re2/glushkov.hh"
#include "ca.hh"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace CA::glushkov {

    Builder::Builder(std::string_view pattern) 
        : regex_(re2::regex::Regex(pattern)), ca_(), range_builder_(),
        range_states_() {
        ca_.set_bytemap(regex_.bytemap());
        ca_.set_bytemap_range(regex_.bytemap_range());

        Fragment frag = compute_fragment(regex_.regexp(), NoCounter);

        // stuff to add if some parts are unanchored
        if (!frag.first.empty()) {
            auto any_loop_start = ca_.add_state(NoCounter);
            if (frag.nullable) {
                ca_.get_state(any_loop_start).set_final(ca_.get_counters());
            }
            add_transition_init(ca_.get_init(), any_loop_start, ca_.bytemap_range());
            add_transition(any_loop_start, any_loop_start, ca_.bytemap_range());
            for (auto &first : frag.first) {
                add_transition(any_loop_start, first.state, first.byte_class);
            }
        }
        if (!frag.last.empty()) {
            auto any_loop_end = ca_.add_state(NoCounter);
            ca_.get_state(any_loop_end).set_final(ca_.get_counters());
            add_transition(any_loop_end, any_loop_end, ca_.bytemap_range());
            for (auto last : frag.last) {
                add_transition(last, any_loop_end, ca_.bytemap_range());
            }
        }

        auto &init = ca_.get_init();

        for (auto &first : frag.first) {
            add_transition_init(init, first.state, first.byte_class);
        }
        for (auto &last : frag.last) {
            ca_.get_state(last).set_final(ca_.get_counters());
        }
        if (frag.nullable) {
            init.set_final(ca_.get_counters());
        }
    }
    
    void Builder::add_transition(StateId o_id, StateId t_id, Symbol symbol) {
        auto &o = ca_.get_state(o_id);
        auto &t = ca_.get_state(t_id);

        if (o.cnt() == NoCounter) {
            if (t.cnt() == NoCounter) {
                o.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Noop});
            } else {
                o.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Rst});
            }
        } else if (t.cnt() == NoCounter) {
            if (ca_.get_counter(o.cnt()).min() == 0) {
                o.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Noop});
            } else {
                o.add_transition(Transition{
                        symbol, t_id, Guard::CanExit, Operator::Noop});
            }
        } else if (o.cnt() != t.cnt()) {
            if (ca_.get_counter(o.cnt()).min() == 0) {
                o.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Rst});
            } else {
                o.add_transition(Transition{
                        symbol, t_id, Guard::CanExit, Operator::Rst});
            }
        } else {
            o.add_transition(Transition{
                    symbol, t_id, Guard::True, Operator::ID});
        }
    }

    // used when the star repetition({0, -1}) is outside the scope of any counter
    void Builder::add_transition_star(StateId o_id, StateId t_id, Symbol symbol) {
        auto &origin = ca_.get_state(o_id);
        auto &target = ca_.get_state(t_id);
        if (origin.cnt() == NoCounter) {
            if (target.cnt() == NoCounter) {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Noop});
            } else {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Rst});
            }
        } else if (target.cnt() == NoCounter) {
            if (ca_.get_counter(origin.cnt()).min() == 0) {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Noop});
            } else {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::CanExit, Operator::Noop});
            }
        } else {
            if (ca_.get_counter(origin.cnt()).min() == 0) {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Rst});
            } else {
                origin.add_transition(Transition{
                        symbol, t_id, Guard::CanExit, Operator::Rst});
            }
        }
    }

    // used in repeat nodes for: last -> first transitions
    void Builder::add_transition_repeat(StateId o_id, StateId t_id, Symbol symbol) {
        auto &origin = ca_.get_state(o_id);
        if (ca_.get_counter(origin.cnt()).max() == - 1) {
            origin.add_transition(Transition{
                    symbol, t_id, Guard::True, Operator::Incr});
        } else {
            origin.add_transition(Transition{
                    symbol, t_id, Guard::CanIncr, Operator::Incr});
        }
    }

    // used to generate transition from the initial state to first(R)
    void Builder::add_transition_init(State &init, StateId t_id, Symbol symbol) {
        auto &target = ca_.get_state(t_id);
        if (target.cnt() == NoCounter) {
            init.add_transition(Transition{
                    symbol, t_id, Guard::True, Operator::Noop});
        } else {
            init.add_transition(Transition{
                    symbol, t_id, Guard::True, Operator::Rst});
        }
    }

    Fragment Builder::get_range_frag(CounterId cnt) {
        auto const& ranges = range_builder_.ranges();
        auto const& root = range_builder_.root();
        range_states_.resize(ranges.size());
        std::fill(range_states_.begin(), range_states_.end(), 0);

        range_states_[0] = ca_.add_state(cnt); // the last state
        Fragment frag = {{}, {range_states_[0]}, false};

        std::unordered_set<uint8_t> ascii_byte_classes;
        std::vector<uint8_t> byte_classes;
        std::vector<uint32_t> stack; // for traversing the inner states
        for (auto id : root) { // iterating over the first ranges
            auto const& range = ranges[id];
            if (range.hi < 0x80) {
                for (uint8_t i = range.lo; i <= range.hi; ++i) {
                    ascii_byte_classes.insert(ca_.get_byte_class(i));
                }
            } else {
                byte_classes.clear();
                for (uint8_t i = range.lo; i <= range.hi; ++i) {
                    auto c = ca_.get_byte_class(i);
                    if (std::find(byte_classes.begin(), byte_classes.end(), c)
                            == byte_classes.end()) {
                      byte_classes.push_back(c);
                    }
                }
                // TODO: here i could probaly use the same state for all
                // successors, but i am not sure if there could be any 
                // cached states that would be already created
                for (auto next : range.next) { 
                    auto s = range_states_[next];
                    if (s == 0) {
                        s = ca_.add_state(cnt);
                        range_states_[next] = s;
                        stack.push_back(next);
                    }
                    for (auto c : byte_classes) {
                        frag.first.push_back(FirstState{s, c});
                    }
                }
            }
        }

        while (!stack.empty()) { // iterating over the inner ranges
            int id = stack.back();
            stack.pop_back();
            auto const& range = ranges[id];
            byte_classes.clear();
            for (uint8_t i = range.lo; i <= range.hi; ++i) {
                auto c = ca_.get_byte_class(i);
                if (std::find(byte_classes.begin(), byte_classes.end(), c)
                        == byte_classes.end()) {
                    byte_classes.push_back(c);
                }
            }
            for (auto next : range.next) {
                auto s = range_states_[next];
                if (s == 0) {
                    s = ca_.add_state(cnt);
                    range_states_[next] = s;
                    stack.push_back(next);
                }
                for (auto c : byte_classes) {
                    add_transition(range_states_[id], s, c);
                }
            }
        }

        for (auto c : ascii_byte_classes) { // finaly adding the ascii bytes
            frag.first.push_back(FirstState{range_states_[0], c});
        }

        return frag;
    }

    Fragment Builder::any_char_frag(CounterId cnt) {
        if (!range_builder_.prepare(static_cast<uint64_t>(re2::kRegexpAnyChar))) {
            range_builder_.add_rune_range(0, 0x10FFFF);
        }
        return get_range_frag(cnt);
    }


    Fragment Builder::char_class_frag(re2::Regexp *re, CounterId cnt) {
        auto cc = re->cc();
        if (!range_builder_.prepare(reinterpret_cast<uint64_t>(re))) {
            for (auto it = cc->begin(); it != cc->end(); ++it) {
                range_builder_.add_rune_range(it->lo, it->hi);
            }
        }
        return get_range_frag(cnt);

    }


    Fragment Builder::lit_frag(re2::Regexp *re, CounterId cnt) {
        char chars[re2::UTFmax];
        int rune = re->rune();
        int len = re2::runetochar(chars, &rune);
        if (len == 1) {
            auto s = ca_.add_state(cnt);
            return Fragment{{FirstState{s, ca_.get_byte_class(chars[0])}}, {s}, false};
        } else {
            Fragment frag{{}, {}, false};
            StateId prev;
            for (auto i = 0; i < len; i++) {
                auto s = ca_.add_state(cnt);
                if (i == 0) {
                    frag.first.push_back(FirstState{s, ca_.get_byte_class(chars[i])});
                } else {
                    add_transition(prev, s, ca_.get_byte_class(chars[i]));
                }
                if (i == len - 1) {
                    frag.last.push_back(s);
                }
                prev = s;
            }
            return frag;
        }
    }

    Fragment Builder::lit_str_frag(re2::Regexp *re, CounterId cnt) {
        assert(re->nrunes() > 0);

        Fragment frag{{}, {}, false};
        char chars[re2::UTFmax * re->nrunes()];
        int base = 0;
        StateId prev;
        for (auto i = 0; i < re->nrunes(); ++i) {
            int rune = re->runes()[i];
            int len = re2::runetochar(chars + base, &rune);
            if (len == 0) {
                throw std::runtime_error("runetochar failed");
            }
            for (auto j = 0; j < len; j++) {
                auto s = ca_.add_state(cnt);
                if (i == 0 && j == 0) {
                    frag.first.push_back(FirstState{s, ca_.get_byte_class(chars[0])});
                } else {
                    add_transition(prev, s, ca_.get_byte_class(chars[base + j]));
                }
                prev = s;
            }
            base += len;
        }
        frag.last.push_back(prev);
        return frag;
    }

    Fragment Builder::concat_frag(re2::Regexp *re, CounterId cnt) {
        Fragment frag{{}, {}, true};
        bool front_anchor = false;
        bool back_anchor = false;
        bool start = true;
        auto subs = re->sub();
        for (auto i = 0; i < re->nsub(); ++i) {
            if (subs[i]->op() == re2::kRegexpBeginText || subs[i]->op() == re2::kRegexpBeginLine) {
                assert(i == 0 && re->nsub() > 1);
                front_anchor = true;
                continue;
            } else if (subs[i]->op() == re2::kRegexpEndText || subs[i]->op() == re2::kRegexpEndLine) {
                assert(i != 0 && i + 1 == re->nsub());
                back_anchor = true;
                continue;
            }
            Fragment sub_frag = compute_fragment(subs[i], cnt);
            if (i != 0) {
                for (auto const& prev : frag.last) {
                    for (auto const& cur: sub_frag.first) {
                        add_transition(prev, cur.state, cur.byte_class);
                    }
                }
            }
            if (start) {
                frag.first.reserve(frag.first.size() + sub_frag.first.size());
                std::move(sub_frag.first.begin(), sub_frag.first.end(), 
                        std::inserter(frag.first, frag.first.end()));
                if (!sub_frag.nullable) {
                    start = false;
                    frag.nullable = false;
                }
            }
            if (sub_frag.nullable) {
                frag.last.reserve(frag.last.size() + sub_frag.last.size());
                std::move(sub_frag.last.begin(), sub_frag.last.end(),
                        std::inserter(frag.last, frag.last.end()));
            } else {
                frag.last = std::move(sub_frag.last);
            }
        }
        if (front_anchor) {
            frag.nullable = false;
            for (auto const& first : frag.first) {
                add_transition(InitState, first.state, first.byte_class);
            }
            frag.first.clear();
        }
        if (back_anchor) {
            frag.nullable = false;
            for (auto const& last : frag.last) {
                ca_.get_state(last).set_final(ca_.get_counters());
            }
            frag.last.clear();
        }
        return frag;
    }

    Fragment Builder::alter_frag(re2::Regexp *re, CounterId cnt) {
        Fragment frag{{}, {}, false};
        auto const& subs = re->sub();
        for (auto i = 0; i < re->nsub(); ++i) {
            Fragment sub_frag = compute_fragment(subs[i], cnt);
            if (sub_frag.nullable) {
                frag.nullable = true;
            }
            frag.first.reserve(frag.first.size() + sub_frag.first.size());
            std::move(sub_frag.first.begin(), sub_frag.first.end(), 
                    std::inserter(frag.first, frag.first.end()));
            frag.last.reserve(frag.last.size() + sub_frag.last.size());
            std::move(sub_frag.last.begin(), sub_frag.last.end(),
                    std::inserter(frag.last, frag.last.end()));
        }
        return frag;
    }

    Fragment Builder::build_star_frag(Fragment frag, CounterId cnt) {
        if (cnt == 0) {
            for (auto const& prev : frag.last) {
                for (auto const& cur: frag.first) {
                    add_transition_star(prev, cur.state, cur.byte_class);
                }
            }
        } else {
            for (auto const& prev : frag.last) {
                for (auto const& cur: frag.first) {
                    add_transition(prev, cur.state, cur.byte_class);
                }
            }
        }
        frag.nullable = true;
        return frag;
    }

    Fragment Builder::star_frag(re2::Regexp *re, CounterId cnt) {
        return build_star_frag(compute_fragment(re->sub()[0], cnt), cnt);
    }

    Fragment Builder::plus_frag(re2::Regexp *re, CounterId cnt) {
        Fragment frag1 = compute_fragment(re->sub()[0], cnt);
        if (frag1.nullable) {
            return build_star_frag(frag1, cnt);
        }
        Fragment frag2 = build_star_frag(compute_fragment(re->sub()[0], cnt), cnt);
        for (auto const& f1_last : frag1.last) {
            for (auto const& f2_first : frag2.first) {
                add_transition(f1_last, f2_first.state, f2_first.byte_class);
            }
            frag2.last.reserve(frag2.last.size() + frag1.last.size());
            std::move(frag1.last.begin(), frag1.last.end(), 
                    std::inserter(frag2.last, frag2.last.end()));
        }
        return Fragment{std::move(frag1.first), std::move(frag2.last), false};
    }

    Fragment Builder::quest_frag(re2::Regexp *re, CounterId cnt) {
        Fragment frag = compute_fragment(re->sub()[0], cnt);
        frag.nullable = true;
        return frag;
    }

    Fragment Builder::any_byte_frag(CounterId cnt) {
        auto s = ca_.add_state(cnt);
        return Fragment{{FirstState{s, ca_.bytemap_range()}}, {s}, false};
    }

    Fragment Builder::repeat_frag(re2::Regexp *re, CounterId cnt) {
        if (re->min() == 0 && re->max() == -1) {
            return star_frag(re, cnt);
        } else {
            if (cnt != 0) {
                throw std::logic_error("Nested repetition not supported");
            }
            CounterId new_cnt = ca_.add_counter(Counter(re->min(), re->max()));
            Fragment frag = compute_fragment(re->sub()[0], new_cnt);
            if (frag.nullable) {
                ca_.get_counter(new_cnt).set_min(0);
            }
            for (auto const& prev: frag.last) {
                for (auto const& cur: frag.first) {
                    add_transition_repeat(prev, cur.state, cur.byte_class);
                }
            }
            if (re->min() == 0) {
                frag.nullable = true;
            }
            return frag;

        }
    }

    Fragment Builder::compute_fragment(re2::Regexp *re, CounterId cnt) {
        switch (re->op()) {
            case re2::kRegexpNoMatch:
            case re2::kRegexpEmptyMatch:
                return Fragment{{}, {}, true};
            case re2::kRegexpLiteral:
                return lit_frag(re, cnt);
            case re2::kRegexpLiteralString:
                return lit_str_frag(re, cnt);
            case re2::kRegexpConcat:
                return concat_frag(re, cnt);
            case re2::kRegexpAlternate:
                return alter_frag(re, cnt);
            case re2::kRegexpStar:
                return star_frag(re, cnt);
            case re2::kRegexpRepeat:
                return repeat_frag(re, cnt);
            case re2::kRegexpPlus: 
                return plus_frag(re, cnt);
            case re2::kRegexpQuest:
                return quest_frag(re, cnt);
            case re2::kRegexpAnyChar: // TODO: support for multibyte bytes
                return any_char_frag(cnt);
            case re2::kRegexpAnyByte:
                return any_byte_frag(cnt);
            case re2::kRegexpCharClass:
                return char_class_frag(re, cnt);
            case re2::kRegexpCapture:
                return compute_fragment(re->sub()[0], cnt);
            case re2::kRegexpBeginLine:
            case re2::kRegexpEndLine:
            case re2::kRegexpWordBoundary:
            case re2::kRegexpNoWordBoundary:
            case re2::kRegexpBeginText:
            case re2::kRegexpEndText:
            default:
                throw std::runtime_error("Use of unimplemented RegexpOp: "s
                        + std::to_string(re->op()) + " in get_eq()"s);
        }
    }
} // namespace CA::glushkov
