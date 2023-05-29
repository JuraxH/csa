#pragma once

//#include "re2/re2.h"
//#include "re2/regexp.h"

#include <vector>
#include <string>
#include <cassert>

namespace CA {

    using namespace std::string_literals;
    using std::string;

    using CounterId = unsigned;
    using Symbol = uint8_t;
    using StateId = unsigned;

    // id for places without counter
    const CounterId NoCounter = 0;

    template<typename T>
    class CounterType {
        public:
            CounterType(T min, T max) : min_(min), max_(max) {}

            T min() const { return min_; }
            T max() const { return max_; }
            string to_string() const {
                return "{min: "s + std::to_string(min_) + " max: "s + std::to_string(max_) + "}"s;
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

    string operator_to_string(Operator op) {
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

    string guard_to_string(Guard grd) {
        switch (grd) {
            using enum Guard;
            case True:      return "True";
            case False:     return "False";
            case CanIncr:   return "CanIncr";
            case CanExit:   return "CanExit";
        }
        return "";
    }

    class Transition {
        public:
            Transition(Symbol symbol, StateId target, 
                    Guard grd, Operator op) : symbol_(symbol),
                    target_(target), grd_(grd), op_(op) {} 

            Symbol symbol() const { return symbol_; }
            StateId target() const { return target_; }
            Guard &grd() { return grd_; }
            Operator &op() { return op_; }
            string to_string() const {
                string str{};
                str += "{B:"s + std::to_string(symbol_) + "|T:"s + std::to_string(target_) + "|G:"s;
                str += guard_to_string(grd_);
                str += "|O:"s;
                str += operator_to_string(op_);
                return str + "}";
            }
            string to_DOT() const {
                string str{};
                str += std::to_string(target_) + "[label=\""s + std::to_string(symbol_) + "|G:"s;
                str += guard_to_string(grd_);
                str += "|O:";
                str += operator_to_string(op_);
                return str + "\"]"s;
            }
        private:
            Symbol symbol_;
            StateId target_;
            Guard grd_;
            Operator op_;
    };

    class State {
        public:
            using Transitions = std::vector<Transition>;
            State () : transitions_(), cnt_(0), final_(Guard::False) {}

            void add_transition(Transition &&trans) { transitions_.push_back(trans); }
            Transitions &transitions() { return transitions_; }
            void set_final(Counters const &cnts) { 
                if (cnt_ && cnts[cnt_ - 1].min() != 0) {
                    final_ = Guard::CanExit; 
                } else {
                    final_ = Guard::True; 
                } 
            }
            Guard final() const { return final_; }

            string to_string(string indent="") const {
                string str{};
                str += "{F:"s + guard_to_string(final_) + "|Transitions:\n"s;
                for (auto& trans : transitions_) {
                    str += indent + "\t"s + trans.to_string() + "\n"s;
                }
                return str + indent + "}"s;
            }
            string to_DOT(StateId id) const {
                string str{};
                string str_id = std::to_string(id);
                str += str_id + "[label=\""s + str_id + "|"s + guard_to_string(final_) + "\"]\n"s;
                for (auto const &t : transitions_) {
                    str += str_id + " -> " + t.to_DOT() + "\n";
                }
                return str;
            }

        private:
            Transitions transitions_;
            CounterId cnt_;
            Guard final_;
    };

    using States = std::vector<State>;

    class CA {
        public:


        CA() : counters_(), states_() {}

        State& get_state(StateId id) { assert(id < states_.size()); return states_[id]; }
        StateId add_state() { states_.push_back(State()); return states_.size() - 1; }
        size_t state_count() { return states_.size(); }

        Counter& get_counter(CounterId id) { assert(id - 1 < counters_.size()); return counters_[id-1]; }
        CounterId add_counter(Counter &&cnt) { counters_.push_back(cnt); return counters_.size(); }
        Counters& get_counters() { return counters_; }
        size_t counter_count() { return counters_.size(); }

        string to_string() {
            string str{};
            str += "CA {\n\tCounters:\n"s;
            for (auto i = 1; i < counters_.size(); i++) {
                str += "\t\t"s + std::to_string(i) + ": "s + counters_[i].to_string() + "\n"s;
            }
            str += "\tStates:\n"s;
            for (auto i = 0; i < states_.size(); i++) {
                str += "\t\t"s + std::to_string(i) + ": "s + states_[i].to_string("\t\t") + "\n"s;
            }
            return str + "}"s;
        }

        string to_DOT() const {
            string str{"digraph CA {\n"s};
            for (auto i = 0; i < states_.size(); i++) {
                str += states_[i].to_DOT(i);
            }
            return str + "}\n"s;
        }

        private:
            // array of all counters in CA, index is counter ID
            Counters counters_; 
            // stores final condition and transitions for each state
            // index indentifies state
            States states_;
    };
}
