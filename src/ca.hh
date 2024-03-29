#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>
#include <string>
#include <cassert>
#include <sstream>

#include <iostream>

namespace CA {

    using namespace std::string_literals;
    using std::string;

    using CounterId = unsigned;
    using StateId = unsigned;

    // counter id for states without counter
    const CounterId NoCounter = 0;
    const size_t ByteMapSize = 256;
    const StateId InitState = 0;

    inline std::string to_str_range(uint16_t symbol) {
        std::ostringstream oss;
        auto hi = symbol>>8;
        auto lo = symbol&0xFF;
        if (hi >= '!' && hi <= '~') { oss << static_cast<char>(hi); } else { oss << "0x" << std::hex << hi; }
        if (lo == hi) return oss.str();
        if (lo >= '!' && lo <= '~') { oss << '-' << static_cast<char>(lo); } else { oss << "-0x" << std::hex << lo; }
        return oss.str();
    }

    template<typename T>
    class CounterType {
        public:
        CounterType(T min, T max) : min_(min), max_(max) {}

        [[nodiscard]] T min() const { return min_; }
        [[nodiscard]] T max() const { return max_; }

        void set_min(T min) { min_ = min; }

        [[nodiscard]] bool can_incr(unsigned val) const {
            return max_ == -1 || static_cast<unsigned>(max_) > val;
        }

        [[nodiscard]] bool can_exit(unsigned val) const {
            return static_cast<unsigned>(min_) <= val;
        }
        [[nodiscard]] string to_string() const {
            return "{min: "s + std::to_string(min_) + " max: "s 
                + std::to_string(max_) + "}"s;
        }
        private:
        T min_;
        T max_;
    };

    using Counter = CounterType<int>;
    using Counters = std::vector<Counter>; 

    enum class Operator {
        Incr,
        Rst,
        ID,
        Noop,
    };

    [[nodiscard]] inline string operator_to_string(Operator op) {
        switch (op) {
            using enum Operator;
            case Incr:  return "Incr";
            case Rst:   return "Rst";
            case ID:    return "ID";
            case Noop:  return "Noop";
        }
        return "";
    }

    enum class Guard {
        True,
        False,
        CanIncr,
        CanExit,
    };

    [[nodiscard]] inline string guard_to_string(Guard grd) {
        switch (grd) {
            using enum Guard;
            case True:      return "True";
            case False:     return "False";
            case CanIncr:   return "CanIncr";
            case CanExit:   return "CanExit";
        }
        return "";
    }

    template<typename SymbolT>
    class TransitionT {
        public:
        TransitionT(SymbolT symbol, StateId target, 
                Guard grd, Operator op) : symbol_(symbol),
                target_(target), grd_(grd), op_(op) {} 

        [[nodiscard]] SymbolT symbol() const { return symbol_; }
        [[nodiscard]] StateId target() const { return target_; }
        [[nodiscard]] Guard grd() const { return grd_; }
        [[nodiscard]] Operator op() const { return op_; }

        [[nodiscard]] string to_string() const {
            string str{};
            str += "{B:"s + std::to_string(symbol_) + "|T:"s 
                + std::to_string(target_) + "|G:"s
                + guard_to_string(grd_)
                + "|O:"s
                + operator_to_string(op_);
            return str + "}";
        }

        [[nodiscard]] string to_DOT(std::function<std::string(SymbolT)> s_to_str) const {
            string str{};
            str += std::to_string(target_) + "[label=\""s 
                + s_to_str(symbol_) + "|G:"s
                + guard_to_string(grd_)
                + "|O:"
                + operator_to_string(op_);
            return str + "\"]"s;
        }

        private:
            SymbolT symbol_;
            StateId target_;
            Guard grd_;
            Operator op_;
    };

    template<typename SymbolT>
    class StateT {
        public:
            using Transition = TransitionT<SymbolT>;
            using Transitions = std::vector<Transition>;

            StateT () : transitions_(), cnt_(0), final_(Guard::False) {}
            StateT (CounterId cnt) : transitions_(), cnt_(cnt), final_(Guard::False) {}

            void add_transition(Transition &&trans) { transitions_.push_back(trans); }

            [[nodiscard]] Transitions const& transitions() const { return transitions_; }
            [[nodiscard]] Guard final() const { return final_; }
            [[nodiscard]] CounterId cnt() const { return cnt_; } 

            void set_final(Counters const &cnts) { 
                if (cnt_ && cnts[cnt_ - 1].min() != 0) {
                    final_ = Guard::CanExit; 
                } else {
                    final_ = Guard::True; 
                } 
            }


            [[nodiscard]] string to_string(string indent="") const {
                string str{};
                str += "{F:"s + guard_to_string(final_) + "|C:"s 
                    + std::to_string(cnt_) + "|Transitions:\n"s;
                for (auto& trans : transitions_) {
                    str += indent + "\t"s + trans.to_string() + "\n"s;
                }
                return str + indent + "}"s;
            }

            [[nodiscard]] string to_DOT(StateId id, std::function<std::string(SymbolT)> s_to_str) const {
                string str{};
                string str_id = std::to_string(id);
                str += str_id + "[label=\""s + str_id + "|"s 
                    + guard_to_string(final_) + "|C:"s 
                    + std::to_string(cnt_) + "\"]\n"s;
                for (auto const &t : transitions_) {
                    str += str_id + " -> " + t.to_DOT(s_to_str) + "\n";
                }
                return str;
            }

        private:
            Transitions transitions_;
            CounterId cnt_;
            Guard final_;
    };


    template<typename SymbolT>
    class CA {
        public:
        using State = StateT<SymbolT>;
        using States = std::vector<State>;


        CA() : counters_(), states_({State()}), bytemap_(), bytemap_range_(0) { }

        [[nodiscard]] State& get_state(StateId id) { 
            assert(id < states_.size()); 

            return states_[id]; 
        }

        [[nodiscard]] State const& get_state(StateId id) const { 
            assert(id < states_.size()); 

            return states_[id]; 
        }

        [[nodiscard]] StateId add_state(CounterId cnt=0) { 
            assert(cnt <= counters_.size()); 

            states_.push_back(State(cnt));
            return states_.size() - 1; 
        }
        [[nodiscard]] size_t state_count() const { return states_.size(); }
        [[nodiscard]] State& get_init() { return get_state(InitState); } 

        [[nodiscard]] Counter& get_counter(CounterId id) { 
            assert(id - 1 < counters_.size());

            return counters_[id-1]; 
        }

        [[nodiscard]] Counter const& get_counter(CounterId id) const { 
            assert(id - 1 < counters_.size());

            return counters_[id-1]; 
        }

        [[nodiscard]] CounterId add_counter(Counter &&cnt) { 
            counters_.push_back(cnt); 
            return counters_.size(); 
        }

        [[nodiscard]] Counters& get_counters() { return counters_; }
        [[nodiscard]] size_t counter_count() const { return counters_.size(); }

        [[nodiscard]] uint8_t get_byte_class(uint8_t byte) const { 
            return bytemap_[byte]; 
        }

        [[nodiscard]] uint8_t bytemap_range() const {
            return bytemap_range_; 
        }

        void set_bytemap_range(uint8_t range) { bytemap_range_ = range; }
        void set_bytemap(uint8_t const* bytemap) { 
            for (unsigned i = 0; i < ByteMapSize; i++) {
                bytemap_[i] = bytemap[i];
            }
        }

        [[nodiscard]] string bytemap_to_str() const {
            string str = "Bytemap {\n"s;
            for (size_t i = 0; i < 256; ++i) {
                if (static_cast<size_t>('!') <= i && static_cast<size_t>('~') >= i) {
                    str += std::to_string(i) + " ["s + static_cast<char>(i) + "]"
                        + " -> "s + std::to_string(bytemap_[i]) + "\n"s;
                } else {
                    str += std::to_string(i) + " -> "s + std::to_string(bytemap_[i]) + "\n"s;
                }
            }
            return str;
        }

        [[nodiscard]] std::unordered_map<uint8_t, string> bytemap_debug() const {
            std::unordered_map<uint8_t, std::vector<std::pair<uint8_t, uint8_t>>> map;
            uint8_t cur_val = bytemap_[0];
            uint8_t cur_start = 0;
            for (size_t i = 0; i < 256; ++i) {
                uint8_t val = bytemap_[i];
                if (cur_val != val) {
                    map[cur_val].push_back({cur_start, static_cast<uint8_t>(i - 1)});
                    cur_val = val;
                    cur_start = static_cast<uint8_t>(i);
                } 
            }
            map[cur_val].push_back({cur_start, static_cast<uint8_t>(255)});
            std::unordered_map<uint8_t, string> res;
            auto to_str = [] (uint8_t val) {
                if (static_cast<size_t>('"') == val) {
                    return "\\\"\\\"\\\""s;
                } else if (static_cast<size_t>('\\') == val) {
                    return "\\\\"s;
                } else if (static_cast<size_t>('!') <= val && static_cast<size_t>('~') >= val) {
                    return "\\\""s + string(1, static_cast<char>(val)) + "\\\""s;
                } else {
                    return std::to_string(val);
                }
            };
            for (auto& [val, ranges] : map) {
                string str = "["s;
                for (auto& [start, end] : ranges) {
                    if (start == end) {
                        str += to_str(start);
                    } else {
                        str += to_str(start) + "-"s + to_str(end);
                    }
                }
                res[val] = str + "]"s;
            }
            return res;
        }

        [[nodiscard]] string to_string() const {
            string str{};
            str += "CA {\n\tCounters:\n"s;
            for (size_t i = 0; i < counters_.size(); i++) {
                str += "\t\t"s + std::to_string(i+1) + ": "s 
                    + counters_[i].to_string() + "\n"s;
            }
            str += "\tStates:\n"s;
            for (size_t i = 0; i < states_.size(); i++) {
                str += "\t\t"s + std::to_string(i) + ": "s 
                    + states_[i].to_string("\t\t") + "\n"s;
            }
            return str + "}"s;
        }

        [[nodiscard]] string to_DOT(std::function<std::string(SymbolT)> s_to_str) const {
            string str{"digraph CA {\n"s};
            for (size_t i = 0; i < states_.size(); i++) {
                str += states_[i].to_DOT(i, s_to_str);
            }
            return str + "}\n"s;
        }

        private:
            // array of all counters in CA, index is counter ID
            Counters counters_; 
            // maps state id to state
            States states_;
            // bytemap
            uint8_t bytemap_[ByteMapSize];
            uint8_t bytemap_range_;
    };
}
