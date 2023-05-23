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
    using Symbol = int;
    using StateId = unsigned;

    // id for places without counter
    const CounterId NoCounter = 0;

    template<typename T>
    class CounterType {
        public:
            CounterType(T min, T max) : min_(min), max_(max) {}

            T min() { return min_; }
            T max() { return max_; }
            string to_string() {
                return "{min: "s + std::to_string(min_) + " max: "s + std::to_string(max_) + "}"s;
            }
        private:
            T min_;
            T max_;
    };

    using Counter = CounterType<int>;


    enum class OperatorEnum {
        Incr,
        Rst,
        ID,
        Exit,
    };

    string operator_to_string(OperatorEnum op) {
        switch (op) {
            using enum OperatorEnum;
            case Incr:  return "Incr";
            case Rst:   return "Rst";
            case ID:    return "ID";
            case Exit:  return "Exit";
        }
        return "";
    }

    enum class GuardEnum {
        True,
        False,
        CanIncr,
        CanExit,
    };

    string guard_to_string(GuardEnum grd) {
        switch (grd) {
            using enum GuardEnum;
            case True:      return "True";
            case False:     return "False";
            case CanIncr:   return "CanIncr";
            case CanExit:   return "CanExit";
        }
        return "";
    }

    class Operator {
        public:
            Operator(OperatorEnum op, CounterId cnt_id) 
                : op_(op), cnt_id_(cnt_id) {}

            OperatorEnum op() const { return op_; }
            CounterId cnt_id() const { return cnt_id_; }
            string to_string() const {
                return "{"s + operator_to_string(op_) + "|"s + std::to_string(cnt_id_) + "}"s;
            }

        private:
            OperatorEnum op_; CounterId cnt_id_;
    };
    class Guard {
        public:
            Guard(GuardEnum grd, CounterId cnt_id) 
                : grd_(grd), cnt_id_(cnt_id) {}

            GuardEnum grd() const { return grd_; }
            CounterId cnt_id() const { return cnt_id_; }
            string to_string() const {
                return "{"s + guard_to_string(grd_) + "|"s + std::to_string(cnt_id_) + "}"s;
            }

        private:
            GuardEnum grd_;
            CounterId cnt_id_;
    };

    class Transition {
        public:
            using Guards = std::vector<Guard>;
            using Operators = std::vector<Operator>;

            Transition(Symbol symbol, StateId target, 
                    Guards &&grds, Operators &&ops) : symbol_(symbol),
                    target_(target), grds_(grds), ops_(ops) {} 

            Symbol symbol() const { return symbol_; }
            StateId target() const { return target_; }
            Guards &grds() { return grds_; }
            Operators &ops() { return ops_; }
            string to_string() const {
                string str{};
                str += "{B:"s + std::to_string(symbol_) + "|T:"s + std::to_string(target_) + "|G:"s;
                for (auto& grd : grds_) {
                    str += grd.to_string();
                }
                str += "|O:"s;
                for (auto &op : ops_) {
                    str += op.to_string();
                }
                return str + "}";
            }
            string to_DOT() const {
                string str{};
                str += std::to_string(target_) + "[label=\""s + std::to_string(symbol_) + "|G:"s;
                for (auto const &grd : grds_) {
                    str += grd.to_string();
                }
                str += "|O:";
                for (auto const &op : ops_) {
                    str += op.to_string();
                }
                return str + "\"]"s;
            }
        private:
            Symbol symbol_;
            StateId target_;
            Guards grds_;
            Operators ops_;
    };

    class State {
        public:
            using Transitions = std::vector<Transition>;
            State () : transitions_(), final_(Guard{GuardEnum::False, NoCounter}) {}

            void add_transition(Transition &&trans) { transitions_.push_back(trans); }
            Transitions &transitions() { return transitions_; }
            void set_final(Guard &&grd) { final_ = grd; }
            Guard final() const { return final_; }

            string to_string(string indent="") const {
                string str{};
                str += "{F:"s + final_.to_string() + "|Transitions:\n"s;
                for (auto& trans : transitions_) {
                    str += indent + "\t"s + trans.to_string() + "\n"s;
                }
                return str + indent + "}"s;
            }
            string to_DOT(StateId id) const {
                string str{};
                string str_id = std::to_string(id);
                str += str_id + "[label=\""s + str_id + "|"s + final_.to_string() + "\"]\n"s;
                for (auto const &t : transitions_) {
                    str += str_id + " -> " + t.to_DOT() + "\n";
                }
                return str;
            }

        private:
            Transitions transitions_;
            Guard final_;
    };

    using States = std::vector<State>;
    using Counters = std::vector<Counter>; 

    class CA {
        public:


        CA() : counters_({Counter{0,0},}), states_() {}

        State& get_state(StateId id) { assert(id < states_.size()); return states_[id]; }
        StateId add_state() { states_.push_back(State()); return states_.size() - 1; }
        size_t state_count() { return states_.size(); }

        Counter& get_counter(CounterId id) { assert(id < counters_.size()); return counters_[id]; }
        CounterId add_counter(Counter &&cnt) { counters_.push_back(cnt); return counters_.size() - 1; }
        size_t counter_count() { return counters_.size() - 1; }

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
