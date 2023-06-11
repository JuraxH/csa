#pragma once

#include "ca.hh"
#include "re2/regex.hh"
#include "re2/ca.hh"
#include <string>
#include <string_view>
#include <iostream>
#include <vector>


namespace CA::glushkov {

    struct FirstState {
        StateId state;
        uint8_t byte_class;
    };

    struct Fragment {
        std::vector<FirstState> first;
        std::vector<StateId> last;
        bool nullable;
    };

    class Builder {
        public:
        static CA get_ca(std::string_view pattern) {
            auto builder = Builder(pattern);
            return builder.ca();
        }

        private:
        CA ca() { return std::move(ca_); }
        Builder(std::string_view pattern) : regex_(re2::regex::Regex(pattern)), ca_() {
            ca_.set_bytemap(regex_.bytemap());
            ca_.set_bytemap_range(regex_.bytemap_range());

            std::cerr << regex_.regexp()->ToString() << std::endl;

            Fragment frag = compute_fragment(regex_.regexp(), NoCounter);

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

        void add_transition(StateId o_id, StateId t_id, Symbol symbol) {
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
        void add_transition_star(StateId o_id, StateId t_id, Symbol symbol) {
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
        void add_transition_repeat(StateId o_id, StateId t_id, Symbol symbol) {
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
        void add_transition_init(State &init, StateId t_id, Symbol symbol) {
            auto &target = ca_.get_state(t_id);
            if (target.cnt() == NoCounter) {
                init.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Noop});
            } else {
                init.add_transition(Transition{
                        symbol, t_id, Guard::True, Operator::Rst});
            }
        }

        Fragment lit_frag(re2::Regexp *re, CounterId cnt) {
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

        Fragment lit_str_frag(re2::Regexp *re, CounterId cnt) {
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

        Fragment concat_frag(re2::Regexp *re, CounterId cnt) {
            Fragment frag{{}, {}, true};
            bool start = true;
            auto subs = re->sub();
            for (auto i = 0; i < re->nsub(); ++i) {
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
            return frag;
        }

        Fragment alter_frag(re2::Regexp *re, CounterId cnt) {
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

        Fragment build_star_frag(Fragment frag, CounterId cnt) {
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

        Fragment star_frag(re2::Regexp *re, CounterId cnt) {
            return build_star_frag(compute_fragment(re->sub()[0], cnt), cnt);
        }

        Fragment plus_frag(re2::Regexp *re, CounterId cnt) {
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

        Fragment quest_frag(re2::Regexp *re, CounterId cnt) {
            Fragment frag = compute_fragment(re->sub()[0], cnt);
            frag.nullable = true;
            return frag;
        }

        Fragment any_byte_frag(CounterId cnt) {
            auto s = ca_.add_state(cnt);
            return Fragment{{FirstState{s, ca_.bytemap_range()}}, {s}, false};
        }

        Fragment repeat_frag(re2::Regexp *re, CounterId cnt) {
            if (re->min() == 0 && re->max() == -1) {
                return star_frag(re, cnt);
            } else {
                if (cnt != 0) {
                    throw std::logic_error("Nested repetition not supported");
                }
                CounterId new_cnt = ca_.add_counter(Counter(re->min(), re->max()));
                Fragment frag = compute_fragment(re->sub()[0], new_cnt);
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

        Fragment compute_fragment(re2::Regexp *re, CounterId cnt) {
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
                    throw std::runtime_error("Use of unimplemented RegexpOp: "s
                            + std::to_string(re->op()) + " in get_eq()"s);
                case re2::kRegexpAnyByte:
                    return any_byte_frag(cnt);
                case re2::kRegexpCharClass:
                    throw std::runtime_error("Use of unimplemented RegexpOp: "s
                            + std::to_string(re->op()) + " in get_eq()"s);
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

        re2::regex::Regex regex_;
        CA ca_;
    };
} // namespace CA::glushkov
