#pragma once

#include "re2/ca.hh"
#include "re2/flat_regexp.hh"


namespace CA
{
    using namespace re2::FlatRegexp;
    using std::vector;
    using std::visit;

    class UniqueByte {
        public:
        UniqueByte() = default;
        UniqueByte(uint8_t byte, uint8_t id) : byte_(byte), id_(id) {}

        [[nodiscard]] uint8_t byte() const { return byte_; }
        [[nodiscard]] uint8_t id() const { return id_; }

        [[nodiscard]] string to_str() const {
            return to_string(byte_) + "["s + to_string(id_) + "]"s;
        }

        private:
        uint8_t byte_;
        uint8_t id_;
    };

    struct Fragment {
        // maybe should be sets, but problably are unique anyway
        vector<UniqueByte> first;
        vector<UniqueByte> last;
        bool nullable;
    };

    class Builder {
        private:
        [[nodiscard]] UniqueByte add_state(uint8_t byte, CounterId cnt) {
            assert(byte <= flat_.bytemap_range());
            
            auto id = ca_.add_state(cnt);
            map_to_state[byte].push_back(id);
            return UniqueByte(byte, map_to_state[byte].size() - 1);
        }

        [[nodiscard]] StateId ub_to_sid(UniqueByte const &ub) const {
            assert(ub.byte() <= flat_.bytemap_range());
            assert(ub.id() < map_to_state[ub.byte()].size());

            return map_to_state[ub.byte()][ub.id()];
        }

        void add_transition(UniqueByte const &o, UniqueByte const &t) {
            auto o_id = ub_to_sid(o);
            auto t_id = ub_to_sid(t);
            auto &origin = ca_.get_state(o_id);
            auto &target = ca_.get_state(t_id);

            if (origin.cnt() == NoCounter) {
                if (target.cnt() == NoCounter) {
                    origin.add_transition(Transition{
                            t.byte(), t_id, Guard::True, Operator::Noop});
                } else {
                    origin.add_transition(Transition{
                            t.byte(), t_id, Guard::CanExit, Operator::Noop});
                }
            } else if (origin.cnt() == NoCounter) {
                origin.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Rst});
            } else if (origin.cnt() != target.cnt()) {
                origin.add_transition(Transition{
                        t.byte(), t_id, Guard::CanExit, Operator::Rst});
            } else {
                origin.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::ID});
            }
        }

        // used when the star repetition({0, -1}) is outside the scope of any counter
        void add_transition_star(UniqueByte const &o, UniqueByte const &t) {
            auto o_id = ub_to_sid(o);
            auto t_id = ub_to_sid(t);
            auto &origin = ca_.get_state(o_id);
            auto &target = ca_.get_state(t_id);
            if (origin.cnt() == NoCounter) {
                if (target.cnt() == NoCounter) {
                    origin.add_transition(Transition{
                            t.byte(), t_id, Guard::True, Operator::Noop});
                } else {
                    origin.add_transition(Transition{
                            t.byte(), t_id, Guard::CanExit, Operator::Noop});
                }
            } else if (origin.cnt() == NoCounter) {
                origin.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Rst});
            } else {
                origin.add_transition(Transition{
                        t.byte(), t_id, Guard::CanExit, Operator::Rst});
            }
        }

        // used in repeat nodes for: last -> first transitions
        void add_transition_repeat(UniqueByte const &o, UniqueByte const &t) {
            auto o_id = ub_to_sid(o);
            auto t_id = ub_to_sid(t);
            auto &origin = ca_.get_state(o_id);
            origin.add_transition(Transition{
                    t.byte(), t_id, Guard::CanIncr, Operator::Incr});
        }

        // used to generate transition from the initial state to first(R)
        void add_transition_init(State &init, UniqueByte const &t) {
            auto t_id = ub_to_sid(t);
            auto &target = ca_.get_state(t_id);
            if (target.cnt() == NoCounter) {
                init.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Noop});
            } else {
                init.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Rst});
            }
        }

        // functions that are used by visitor of RegexpNode
        [[nodiscard]] Fragment compute(Epsilon const &eps, CounterId cnt);
        [[nodiscard]] Fragment compute(Byte const &byte, CounterId cnt);
        [[nodiscard]] Fragment compute(Bytes const &bytes, CounterId cnt);
        [[nodiscard]] Fragment compute(Alter const &alter, CounterId cnt);
        [[nodiscard]] Fragment compute(Concat const &concat, CounterId cnt);
        [[nodiscard]] Fragment compute(Repeat const &repeat, CounterId cnt);
        [[nodiscard]] Fragment compute(Plus const &plus, CounterId cnt);

        Builder(string const &pattern) : flat_(pattern), ca_(),
        map_to_state(flat_.bytemap_range() + 1, vector<StateId>{}) { 
            ca_.set_bytemap_range(flat_.bytemap_range());
            ca_.set_bytemap(flat_.bytemap());
            RegexpNode const& top_node = flat_.top_node();
            Fragment frag = visit([this] (auto const & arg) {
                    return this->compute(arg, NoCounter); 
                    }, top_node);
            // initial state -> first(R)
            auto &init_state = ca_.get_init();
            for (auto const& state : frag.first) {
                add_transition_init(init_state, state);
            }
            // setting the final condition for states in last(R)
            for (auto const& state : frag.last) {
                auto id = map_to_state[state.byte()][state.id()];
                ca_.get_state(id).set_final(ca_.get_counters());
            }
            // if R is nullable initial state is final
            if (frag.nullable) {
                init_state.set_final(ca_.get_counters());
            }
        }

        FlatRegexp flat_; // regexp where runes are replaced with byte classes
        CA ca_; // CA that is being built
        vector<vector<StateId>> map_to_state; // map: unique bytes -> CA states

        public:
        // interface for getting the CA
        [[nodiscard]] static CA get_ca(string const &pattern) {
            std::cout << "<< get_ca\n";
            return Builder(pattern).ca_;
        }
    };
}
