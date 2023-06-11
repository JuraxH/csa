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
        void add_transition(StateId o_id, StateId t_id, Symbol symbol);
        // used when the star repetition({0, -1}) is outside the scope of any counter
        void add_transition_star(StateId o_id, StateId t_id, Symbol symbol);
        // used in repeat nodes for: last -> first transitions
        void add_transition_repeat(StateId o_id, StateId t_id, Symbol symbol);
        // used to generate transition from the initial state to first(R)
        void add_transition_init(State &init, StateId t_id, Symbol symbol);
        // interface to compute fragment of any regexp
        Fragment compute_fragment(re2::Regexp *re, CounterId cnt);

        // concrete implementattions for each type of regexp
        Fragment lit_frag(re2::Regexp *re, CounterId cnt);
        Fragment lit_str_frag(re2::Regexp *re, CounterId cnt);
        Fragment concat_frag(re2::Regexp *re, CounterId cnt);
        Fragment alter_frag(re2::Regexp *re, CounterId cnt);
        Fragment build_star_frag(Fragment frag, CounterId cnt);
        Fragment star_frag(re2::Regexp *re, CounterId cnt);
        Fragment plus_frag(re2::Regexp *re, CounterId cnt);
        Fragment quest_frag(re2::Regexp *re, CounterId cnt);
        Fragment any_byte_frag(CounterId cnt);
        Fragment repeat_frag(re2::Regexp *re, CounterId cnt);

        CA ca() { return std::move(ca_); }
        Builder(std::string_view pattern);


        re2::regex::Regex regex_;
        CA ca_;
    };
} // namespace CA::glushkov
