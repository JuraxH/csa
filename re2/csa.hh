#pragma once

#include <cstdint>
#include <list>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <string>

#include "re2/ca.hh"
#include "util/ord_vector.hh"

#define FATAL_ERROR(msg, err) do {\
    std::cerr << "Error:" << __FILE__ << ":" << __LINE__ << ": " << __func__ << ": " \
        << msg << std::endl; \
    exit(static_cast<int>(err));} while(false)


namespace CSA {

    enum class Errors {
        DoubleIncr = 10,
        InternalFailure = 11
    };

    template<typename T>
    using OrdVector = Mata::Util::OrdVector<T>;
    using CounterIndex = unsigned;
    using IndexVec = OrdVector<CounterIndex>;

    class CounterState {
        public:
        CounterState() : state_(), actual_(), postponed_() {};
        CounterState(CA::StateId state, IndexVec actual, IndexVec postponed) :
            state_(state), actual_(actual), postponed_(postponed) {};

        bool operator<(const CounterState &other) const {
            return state_ < other.state_;
        }

        bool operator==(const CounterState &other) const {
            return state_ == other.state_;
        }

        CA::StateId state() const { return state_; }
        IndexVec & actual() const { return actual_; }
        IndexVec & postponed() const { return postponed_; }

        std::string to_str() const;

        private:
        CA::StateId state_;
        // mutable to be able to use the OrdVector as map
        // TODO: Implement Flat map using ordered vector to replace OrdVec here
        mutable IndexVec actual_;
        mutable IndexVec postponed_;
    };

    using CounterStateVec = OrdVector<CounterState>;
    using NormalStateVec = OrdVector<CA::StateId>;

    class State {
        public:
        State(NormalStateVec normal, CounterStateVec counter, unsigned cnt_sets) :
            normal_(normal), counter_(counter), cnt_sets_(cnt_sets) {};

        NormalStateVec & normal() { return normal_; }
        CounterStateVec & counter() { return counter_; }

        NormalStateVec const& normal() const { return normal_; }
        CounterStateVec const& counter() const { return counter_; }

        bool operator==(const State &other) const {
            return normal_ == other.normal_
                && counter_ == other.counter_;
        }

        std::string to_str() const;

        private:
        NormalStateVec normal_;
        CounterStateVec counter_;
        unsigned cnt_sets_;
    };

    // from boost:
    // https://www.boost.org/doc/libs/1_35_0/doc/html/boost/hash_combine_id241013.html
    // license: https://www.boost.org/LICENSE_1_0.txt
    template<typename T> static void hash_combine(size_t &seed, T const &val) {
        seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    }

} // namespace CSA

namespace std {
    template <>
    struct hash<CSA::State> {
        std::size_t operator()(const CSA::State& state) const {
            size_t seed = 0;
            for (CA::StateId ca_state : state.normal()) {
                CSA::hash_combine(seed, state);
            }
            for (CSA::CounterState const& state : state.counter()) {
                CSA::hash_combine(seed, state.state());
                for (CSA::CounterIndex const& index : state.actual()) {
                    CSA::hash_combine(seed, index);
                }
                for (CSA::CounterIndex const& index : state.postponed()) {
                    CSA::hash_combine(seed, index);
                }
            }
            return seed;
        }
    };
}

namespace CSA {

    class CountingSet {
        public:
        CountingSet() = default;
        CountingSet(unsigned val) :  list_({0,}), offset_(val) { }

        CountingSet(CountingSet && other) : list_(std::move(other.list_)),
            offset_(other.offset_) {}
        CountingSet(CountingSet const& other) : list_(other.list_),
            offset_(other.offset_) {}

        unsigned offset() const { return offset_; }
        std::list<unsigned> const& list() const { return list_; }

        CountingSet &operator=(CountingSet &&other) {
            offset_ = other.offset_;
            list_ = std::move(other.list_);
            return *this;
        }

        CountingSet &operator=(CountingSet const& other) {
            offset_ = other.offset_;
            list_ = other.list_;
            return *this;
        }

        void merge(CountingSet &&other);

        void increment(unsigned max);
        void rst_to_1();
        void insert_1();

        std::string to_str() const;

        private:
        std::list<unsigned> list_; 
        unsigned offset_;
    };

    using CntSetVec = std::vector<CountingSet>;

    enum class LValueEnum {
        ID,
        PLUS,
    };

    class LValue {
        public:
        LValue(CA::StateId state, LValueEnum type) : state_(state), type_(type) {};

        CA::StateId state() const { return state_; }
        LValueEnum type() const { return type_; }

        bool operator<(const LValue &other) const { return state_ < other.state_; }
        bool operator==(const LValue &other) const { return this->state_ == other.state_; }

        std::string toString() const;

        private:
        CA::StateId state_;
        mutable LValueEnum type_;
    };

    using LValueRowIndex = unsigned;

    class LValueTable {
        public:
        LValueTable(unsigned size) : tab(size, std::vector<LValue>{}) { }

        void add_lval(LValue lval, LValueRowIndex index) {
            std::vector<LValue> &row = tab[index];
            std::vector<LValue>::iterator it = find(row.begin(), row.end(), lval);
            if (it == row.end()) {
                row.push_back(lval);
            } else if (it->type() != lval.type()) {
                FATAL_ERROR("lvalue both actual and postponed", Errors::DoubleIncr);
            }
        }

        std::string to_str() const;

        private:
        std::vector<std::vector<LValue>> tab;
    };

    class CounterToReset {
        public:
        CounterToReset(CA::CounterId counter, OrdVector<CA::StateId> &&states) 
            : counter(counter), states(std::move(states)) {}

        bool operator==(const CounterToReset &other) const { return counter == other.counter; }
        bool operator==(const CA::CounterId &cnt) const { return counter == cnt; }
        bool operator<(const CounterToReset &other) const { return counter < other.counter; }

        private:
        CA::CounterId counter;
        OrdVector<CA::StateId> states;
    };

    class CountersToReset {
        public:
        void add_state(CA::StateId state, CA::CounterId counter);
        OrdVector<OrdVector<CA::StateId>> get_cnt_set_names();

        private:
        std::vector<CounterToReset> counters;
    };

    enum class CntSetInstEnum {
        Move,
        Incr,
        Insert_1,
        Rst_to_1,
    };

    class CntSetInst {
        public:
        CntSetInst(CntSetInstEnum type, unsigned arg1, unsigned arg2) : 
            type_(type), origin_(arg1), target_(arg2) {}
        CntSetInst(CntSetInstEnum type, unsigned arg) : 
            type_(type), origin_(arg), target_(arg) {}

        std::string to_str() const;

        private:
        CntSetInstEnum type_;
        union {
            struct { // Move, Insert_1, Rst_to_1
                CA::StateId origin_;
                CA::StateId target_;
            };
            struct { // Incr
                CA::StateId index_;
                unsigned max_; // needed to filter too large values
            };
        };
    };

    using UpdateProg = std::vector<CntSetInst>;

    struct Trans;

    using CachedState = std::pair<const State, std::vector<Trans>>;

    enum class UpdateEnum {
        Noop,
        KeepSets,
        NewSets,
        Enter,
    };

    // compiled update
    class Update {
        public:
        Update(UpdateEnum type, CachedState *next_state, UpdateProg &&prog) 
            : type_(type), next_state_(next_state), prog_(std::move(prog)) {}
        Update() = default;

        UpdateEnum type() const { return type_; }
        CachedState *next_state() const { return next_state_; }
        UpdateProg const& prog() const { return prog_; }

        std::string to_str() const;

        private:
        UpdateEnum type_;
        CachedState *next_state_;
        UpdateProg prog_;
    };

    struct Guard {
        CA::StateId state;
        CA::Guard condition;
    };
    
    enum class TransEnum {
        NotComputed,
        WithoutCntState,
        EnteringCntState,
        NoCondition,
        Small,
        Lazy,
    };

    const size_t max_conditions_in_small_trans = 2;

    using GuardVec = std::vector<Guard>;
    using GuardedStates = std::vector<std::vector<CA::StateId>>;
    using GuardedLvals = std::vector<std::vector<std::pair<LValue, LValueRowIndex>>>;
    using GuardedResets = std::vector<std::vector<CA::StateId>>;

    class TransBuilder {
        public:
        TransEnum trans_type() const;
        GuardVec const& guards() const { return guards_; }
        private:
        NormalStateVec normal_;
        CounterStateVec counter_;
        LValueTable lvals_;
        CounterToReset cnts_to_reset_;
        GuardVec guards_;
        GuardedStates guarded_states_;
        GuardedLvals guarded_lvals_;
        GuardedResets guarded_resets_;
    };

    using UpdateVec = std::vector<Update>;

    class SmallTrans {
        public:
        SmallTrans(GuardVec &&guards) : guards_(std::move(guards)), updates_() {}

        void add_update(Update &&update) { updates_.push_back(std::move(update)); }
        GuardVec const& guards() const { return guards_; }
        Update const& update(unsigned index) const { return updates_[index]; }

        private:
        GuardVec guards_;
        UpdateVec updates_;
    };

    using UpdateCache = std::unordered_map<uint64_t, Update>;

    class LazyTrans {
        public:
        GuardVec const& guards() const { return builder_.guards(); }
        Update const& update(unsigned index);

        private:
        TransBuilder builder_;
        UpdateCache cache_;
    };

    class Trans {
        Trans() : type_(TransEnum::NotComputed) {}

        void set_next_state(CachedState* next_state, TransEnum type) { type_ = type; next_state_ = next_state; }
        void set_small(SmallTrans* small) { type_ = TransEnum::Small; small = small; }
        void set_update(Update* update) { type_ = TransEnum::NoCondition; update_ = update; }
        void set_lazy(LazyTrans* lazy) { type_ = TransEnum::Lazy; lazy_ = lazy; }


        TransEnum type() const { return type_; }

        CachedState* next_state() const { 
            assert(type_ == TransEnum::WithoutCntState || type_ == TransEnum::EnteringCntState);
            return next_state_; 
        }
        Update* update() { assert(type_ == TransEnum::NoCondition); return update_; }
        SmallTrans* small() { assert(type_ == TransEnum::Small); return small_; }
        LazyTrans* lazy() { assert(type_ == TransEnum::Lazy); return lazy_; }

        std::string to_str() const;

        ~Trans();

        private:
        TransEnum type_;
        union {
            CachedState* next_state_;
            Update* update_;
            SmallTrans* small_;
            LazyTrans* lazy_;
        };
    };

    const State InitialState = State({CA::InitState, }, {}, 0);

    using TransVec = std::vector<Trans>;
    using StateCache = std::unordered_map<State, TransVec>;

    class CSA {
        public:
        CSA(CA::CA<uint8_t> &&ca) : ca_(std::move(ca)), states_() {}

        CachedState* get_state(State const& state);

        std::string to_str() const;

        private:
        CA::CA<uint8_t> ca_;
        StateCache states_;
    };

    class Config {
        public:
        Config(CA::CA<uint8_t> &&ca) : csa_(std::move(ca)), cur_state_(csa_.get_state(InitialState)), cnt_sets_() {}

        void reset() { cur_state_ = init_state_; cnt_sets_.resize(0); }
        bool step(uint8_t c); // true if there is still chance to match
        bool accepting() const;

        private:
        bool eval_guard(CA::Guard guard, CounterState const& cnt_state) const;
        void execute_update(Update const& update);
        unsigned compute_update_index(GuardVec const& guards) const;
        

        CSA csa_;
        CachedState* cur_state_;
        CachedState* init_state_;
        CntSetVec cnt_sets_;
    };

    class Matcher {
        public:
        Matcher(std::string_view pattern);
        bool Match(std::string_view text);

        private:
        Config config_;
    };

} // namespace CSA

