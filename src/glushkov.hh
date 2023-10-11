#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <iostream>
#include <vector>
#include <unordered_map>

#include "ca.hh"
#include "regex.hh"
#include "ca.hh"
#include "range_builder.hh"



namespace CA::glushkov {

    using Symbol = uint8_t;
    using CA = CA<Symbol>;
    using State = StateT<Symbol>;
    using Transition = TransitionT<Symbol>;

    struct FirstState {
        StateId state;
        Symbol byte_class;
    };

    const uint8_t NoAnchor = 0;
    const uint8_t StartAnchor = 1<<0;
    const uint8_t EndAnchor = 1<<1;
    const uint8_t NoStartAlter = 1<<2; // when there is only the anchor
    const uint8_t NoEndAlter = 1<<3;

    struct Fragment {
        std::vector<FirstState> first;
        std::vector<StateId> last;
        bool nullable;
        uint8_t anchor_flag;
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
        
        Fragment get_range_frag(CounterId cnt);

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
        Fragment any_char_frag(CounterId cnt);
        Fragment repeat_frag(re2::Regexp *re, CounterId cnt);
        Fragment char_class_frag(re2::Regexp *re, CounterId cnt);

        CA ca() { return std::move(ca_); }
        Builder(std::string_view pattern);


        re2::regex::Regex regex_;
        CA ca_;
        re2::range_builder::Builder range_builder_;
        std::vector<StateId> range_states_; // used by charclass construction
    };
} // namespace CA::glushkov
