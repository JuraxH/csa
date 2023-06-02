#pragma once

#include <vector>
#include <string>
#include <cassert>

namespace CA {

    using namespace std::string_literals;
    using std::string;

    using CounterId = unsigned;
    using Symbol = uint8_t;
    using StateId = unsigned;

    // counter id for states without counter
    const CounterId NoCounter = 0;

    template<typename T>
    class CounterType {
        public:
        CounterType(T min, T max) : min_(min), max_(max) {}

        [[nodiscard]] T min() const { return min_; }
        [[nodiscard]] T max() const { return max_; }
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

    class Transition {
        public:
        Transition(Symbol symbol, StateId target, 
                Guard grd, Operator op) : symbol_(symbol),
                target_(target), grd_(grd), op_(op) {} 

        [[nodiscard]] Symbol symbol() const { return symbol_; }
        [[nodiscard]] StateId target() const { return target_; }
        [[nodiscard]] Guard &grd() { return grd_; }
        [[nodiscard]] Operator &op() { return op_; }

        [[nodiscard]] string to_string() const {
            string str{};
            str += "{B:"s + std::to_string(symbol_) + "|T:"s 
                + std::to_string(target_) + "|G:"s
                + guard_to_string(grd_)
                + "|O:"s
                + operator_to_string(op_);
            return str + "}";
        }

        [[nodiscard]] string to_DOT() const {
            string str{};
            str += std::to_string(target_) + "[label=\""s 
                + std::to_string(symbol_) + "|G:"s
                + guard_to_string(grd_)
                + "|O:"
                + operator_to_string(op_);
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
            State (CounterId cnt) : transitions_(), cnt_(cnt), final_(Guard::False) {}

            void add_transition(Transition &&trans) { transitions_.push_back(trans); }
            [[nodiscard]] Transitions &transitions() { return transitions_; }

            void set_final(Counters const &cnts) { 
                if (cnt_ && cnts[cnt_ - 1].min() != 0) {
                    final_ = Guard::CanExit; 
                } else {
                    final_ = Guard::True; 
                } 
            }

            [[nodiscard]] Guard final() const { return final_; }
            [[nodiscard]] CounterId cnt() const { return cnt_; } 

            [[nodiscard]] string to_string(string indent="") const {
                string str{};
                str += "{F:"s + guard_to_string(final_) + "|C:"s 
                    + std::to_string(cnt_) + "|Transitions:\n"s;
                for (auto& trans : transitions_) {
                    str += indent + "\t"s + trans.to_string() + "\n"s;
                }
                return str + indent + "}"s;
            }

            [[nodiscard]] string to_DOT(StateId id) const {
                string str{};
                string str_id = std::to_string(id);
                str += str_id + "[label=\""s + str_id + "|"s 
                    + guard_to_string(final_) + "|C:"s 
                    + std::to_string(cnt_) + "\"]\n"s;
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


        CA() : counters_(), states_({State()}) { }

        [[nodiscard]] State& get_state(StateId id) { 
            assert(id < states_.size()); 
            return states_[id]; 
        }

        [[nodiscard]] StateId add_state(CounterId cnt=0) { 
            assert(cnt <= counters_.size()); 
            states_.push_back(State(cnt));
            return states_.size() - 1; 
        }
        [[nodiscard]] size_t state_count() const { return states_.size(); }
        [[nodiscard]] State& get_init() { return get_state(0); } 

        [[nodiscard]] Counter& get_counter(CounterId id) { 
            assert(id - 1 < counters_.size());
            return counters_[id-1]; 
        }

        [[nodiscard]] CounterId add_counter(Counter &&cnt) { 
            counters_.push_back(cnt); 
            return counters_.size(); 
        }

        [[nodiscard]] Counters& get_counters() { return counters_; }
        [[nodiscard]] size_t counter_count() const { return counters_.size(); }

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

        [[nodiscard]] string to_DOT() const {
            string str{"digraph CA {\n"s};
            for (size_t i = 0; i < states_.size(); i++) {
                str += states_[i].to_DOT(i);
            }
            return str + "}\n"s;
        }

        private:
            // array of all counters in CA, index is counter ID
            Counters counters_; 
            // maps state id to state
            States states_;
    };
}
