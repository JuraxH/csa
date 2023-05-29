#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/prog.h"
#include "re2/ca.hh"
#include "re2/flat_regexp.hh"


namespace CA
{
    using std::vector;
    using namespace re2::FlatRegexp;
    using std::visit;

    class UniqueByte {
        public:
        UniqueByte() = default;
        UniqueByte(uint8_t byte, uint8_t id) : byte_(byte), id_(id) {}
        UniqueByte(UniqueByte const &) = default;
        UniqueByte(UniqueByte &&) = default;
        UniqueByte(UniqueByte &) = default;
        UniqueByte& operator=(UniqueByte&&) = default;
        UniqueByte& operator=(UniqueByte&) = default;
        UniqueByte& operator=(UniqueByte const &) = default;

        uint8_t byte() const { return byte_; }
        uint8_t id() const { return id_; }

        string to_str() const {
            if (byte_ >= static_cast<uint8_t>('!') && byte_ <= static_cast<uint8_t>('~')) {
                return string(1, static_cast<char>(byte_)) + "["s + to_string(id_) + "]"s;;
            } else {
                return to_string(byte_) + "["s + to_string(id_) + "]"s;;
            }
        }
        private:
        uint8_t byte_;
        uint8_t id_;
    };

    struct Fragment {
        // maybe should be sets, but problably not
        vector<UniqueByte> first;
        vector<UniqueByte> last;
        bool nullable;
    };

    class Builder {
        private:
        UniqueByte add_state(uint8_t byte, CounterId cnt) {
            auto id = ca_.add_state(cnt);
            map_to_state[byte].push_back(id);
            return UniqueByte(byte, map_to_state[byte].size() - 1);
        }

        void add_transition(UniqueByte const &o, UniqueByte const &t) {
            auto o_id = map_to_state[o.byte()][o.id()];
            auto t_id = map_to_state[t.byte()][t.id()];
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
            auto o_id = map_to_state[o.byte()][o.id()];
            auto t_id = map_to_state[t.byte()][t.id()];
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

        void add_transition_repeat(UniqueByte const &o, UniqueByte const &t) {
            auto o_id = map_to_state[o.byte()][o.id()];
            auto t_id = map_to_state[t.byte()][t.id()];
            auto &origin = ca_.get_state(o_id);
            origin.add_transition(Transition{
                    t.byte(), t_id, Guard::CanIncr, Operator::Incr});
        }

        void add_transition_init(State &init, UniqueByte const &t) {
            auto t_id = map_to_state[t.byte()][t.id()];
            auto &target = ca_.get_state(t_id);
            if (target.cnt() == NoCounter) {
                init.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Noop});
            } else {
                init.add_transition(Transition{
                        t.byte(), t_id, Guard::True, Operator::Rst});
            }
        }

        Fragment compute(Epsilon const &eps, CounterId cnt);
        Fragment compute(Byte const &byte, CounterId cnt);
        Fragment compute(Bytes const &bytes, CounterId cnt);
        Fragment compute(Alter const &alter, CounterId cnt);
        Fragment compute(Concat const &concat, CounterId cnt);
        Fragment compute(Repeat const &repeat, CounterId cnt);
        Fragment compute(Plus const &plus, CounterId cnt);

        Builder(string const &pattern) : flat_(pattern), ca_(),
                map_to_state(flat_.bytemap_range() + 1, vector<StateId>{}) { 
            RegexpNode const& top_node = flat_.top_node();
            std::cout << "<< Builder: " << visit([](auto const& arg) { return arg.to_str(); }, top_node) << "\n";
            Fragment frag = visit([this] (auto const & arg) { return this->compute(arg, NoCounter); }, top_node);
            auto &init_state = ca_.get_init();
            for (auto const& state : frag.first) {
                add_transition_init(init_state, state);
            }
            for (auto const& state : frag.last) {
                auto id = map_to_state[state.byte()][state.id()];
                ca_.get_state(id).set_final(ca_.get_counters());
            }
            if (frag.nullable) {
                init_state.set_final(ca_.get_counters());
            }
        }



        FlatRegexp flat_;
        CA ca_;
        vector<vector<StateId>> map_to_state;

        public:
        static CA get_ca(string const &pattern) {
            std::cout << "<< get_ca\n";
            return Builder(pattern).ca_;
        }
    };
}
