#pragma once

#include <cstdint>
#include <list>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <unordered_map>
#include <string>

#include "ca.hh"
#include "ord_vector.hh"
#include "csa_errors.hh"


// log options: conditions, cnt_sets, config,
#if 0
#define CSA_LOG_CONFIG 1
#define CSA_LOG_EVAL_GUARD 1
#define CSA_LOG_UPDATE 1
#define CSA_LOG_BUILDER 1
#endif

#ifdef CSA_LOG_CONFIG
#define LOG_CONFIG(state, cnt_sets) std::cerr << "<<< next loop >>>\n=================\nLOG_CONFIG:\nSTATE:\n" << state << "CNT_SETS:\n" << cnt_sets << std::endl;
#define LOG_CONFIG_SYMBOL(byte, symbol) std::cerr << "LOG_CONFIG: BYTE: " << byte << " -> SYMBOL: " << symbol << std::endl;
#else
#define LOG_CONFIG(state, cnt_sets) 
#define LOG_CONFIG_SYMBOL(byte, symbol) 
#endif

#ifdef CSA_LOG_EVAL_GUARD
#define LOG_EVAL_GUARD(condition, cnt_state) std::cerr << "LOG_EVAL_GUARD: CONDITION: " << condition << " CNT_STATE: " << cnt_state;
#define LOG_EVAL_GUARD_RES(result) std::cerr << " RES: " << result << std::endl;
#define LOG_EVAL_GUARD_MIN(min) std::cerr << " MIN: " << min;
#define LOG_EVAL_GUARD_MAX(max) std::cerr << " MAX: " << max;
#else
#define LOG_EVAL_GUARD(state, cnt_sets)  
#define LOG_EVAL_GUARD_RES(resutl)  
#define LOG_EVAL_GUARD_MIN(min)  
#define LOG_EVAL_GUARD_MAX(max) 
#endif

#ifdef CSA_LOG_UPDATE
#define LOG_UPDATE(update) std::cerr << "LOG_UPDATE:\n" << update << std::endl;
#else
#define LOG_UPDATE(update) 
#endif

#ifdef CSA_LOG_BUILDER
#define LOG_BUILDER(builder) std::cerr << "LOG_BUILDER:\n" << builder << std::endl;
#else
#define LOG_BUILDER(builder) 
#endif

namespace CSA {

    template<typename T>
    using OrdVector = Mata::Util::OrdVector<T>;
    using CounterIndex = unsigned;
    using IndexVec = OrdVector<CounterIndex>;

    class CounterState {
        public:
        CounterState() : state_(), actual_(), postponed_() {};
        CounterState(CA::StateId state) : state_(state), actual_(), postponed_() {};
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
    class CSA;

    class State {
        public:
        State(NormalStateVec &&normal, CounterStateVec &&counter, unsigned cnt_sets) :
            normal_(std::move(normal)), counter_(std::move(counter)), cnt_sets_(cnt_sets) {};

        State(State const& other) = default;

        NormalStateVec & normal() { return normal_; }
        CounterStateVec & counter() { return counter_; }

        NormalStateVec const& normal() const { return normal_; }
        CounterStateVec const& counter() const { return counter_; }
        unsigned cnt_sets() const { return cnt_sets_; }

        bool operator==(const State &other) const {
            return normal_ == other.normal_
                && counter_ == other.counter_;
        }

        bool dead() const { return normal_.empty() && counter_.empty(); }

        std::string to_str() const;
        std::string DOT_label(CSA const& csa) const;

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
                CSA::hash_combine(seed, ca_state);
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
        CountingSet() : list_(), offset_(1) { }
        CountingSet(unsigned val) :  list_({0,}), offset_(val) { }

        CountingSet(CountingSet && other) : list_(std::move(other.list_)),
            offset_(other.offset_) {}
        CountingSet(CountingSet const& other) : list_(other.list_),
            offset_(other.offset_) {}

        unsigned offset() const { return offset_; }
        std::list<unsigned> const& list() const { return list_; }

        unsigned max() const { return offset_ - list_.back(); }
        unsigned min() const { return offset_ - list_.front(); }

        unsigned max_postponed(int max) const;
        unsigned min_postponed() const;

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

        void increment(int max);
        void rst_to_1();
        void insert_1();

        // for testing only
        std::vector<unsigned> to_vec() const;

        std::string to_str() const;

        private:
        std::list<unsigned> list_; 
        unsigned offset_;
    };

    using CntSetVec = std::vector<CountingSet>;

    enum class LValueEnum {
        ID,
        Plus,
    };

    class LValue {
        public:
        LValue(CA::StateId state, LValueEnum type) : state_(state), type_(type) {};

        CA::StateId state() const { return state_; }
        LValueEnum type() const { return type_; }
        void set_type(LValueEnum type) { type_ = type; }

        bool operator<(const LValue &other) const { return state_ < other.state_; }
        bool operator==(const LValue &other) const { return this->state_ == other.state_; }

        std::string to_str() const;

        private:
        CA::StateId state_;
        mutable LValueEnum type_;
    };

    using LValueRowIndex = unsigned;

    class LValueTable {
        public:
        LValueTable(unsigned size) : tab_(size, std::vector<LValue>{}) { }

        void add_lval(CA::StateId state, LValueEnum type, LValueRowIndex index);
        size_t size() const { return tab_.size(); }
        std::vector<LValue> & operator[](LValueRowIndex index) { return tab_[index]; }

        std::string to_str() const;

        private:
        std::vector<std::vector<LValue>> tab_;
    };

    class CounterToReset {
        public:
        CounterToReset() : counter(), states() {}
        CounterToReset(CA::CounterId counter) : counter(counter), states() {}
        CounterToReset(CA::CounterId counter, CA::StateId state) : counter(counter), states({state,}) {}
        CounterToReset(CA::CounterId counter, OrdVector<CA::StateId> &&states) 
            : counter(counter), states(std::move(states)) {}

        OrdVector<CA::StateId> get_states() { return std::move(states); }
        void add_state(CA::StateId state) { states.insert(state); }

        bool operator==(const CounterToReset &other) const { return counter == other.counter; }
        bool operator==(const CA::CounterId &cnt) const { return counter == cnt; }
        bool operator<(const CounterToReset &other) const { return counter < other.counter; }

        std::string to_str() const;

        private:
        CA::CounterId counter;
        mutable OrdVector<CA::StateId> states;
    };

    class CountersToReset {
        public:
        CountersToReset() : counters() {}
        void add_state(CA::StateId state, CA::CounterId counter);
        OrdVector<OrdVector<CA::StateId>> get_cnt_set_names();

        std::string to_str() const;

        private:
        OrdVector<CounterToReset> counters;
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

        CntSetInstEnum type() const { return type_; }
        unsigned origin() const { return origin_; }
        unsigned target() const { return target_; }
        unsigned index() const { return index_; }
        int max() const { return max_; }

        bool is_move() const { return type_ == CntSetInstEnum::Move && origin_ != target_; }

        std::string to_str() const;

        private:
        CntSetInstEnum type_;
        union {
            struct { // Move, Insert_1, Rst_to_1
                unsigned origin_;
                unsigned target_;
            };
            struct { // Incr
                unsigned index_;
                int max_; // needed to filter too large values
            };
        };
    };

    using UpdateProg = std::vector<CntSetInst>;

    struct Trans;

    using CachedState = std::pair<const State, std::vector<Trans>>;

    enum class UpdateEnum {
        Out,
        Noop,
        KeepSets,
        NewSets,
    };

    // compiled update
    class Update {
        public:
        Update(UpdateEnum type, CachedState *next_state, UpdateProg &&prog) 
            : type_(type), next_state_(next_state), prog_(std::move(prog)) {}
        Update() : type_(), next_state_(), prog_() {}

        Update(Update&&) = default;
        Update(Update const&) = default;
        ~Update() = default;
        Update& operator=(Update const&) = default;

        void add_inst(CntSetInstEnum type, unsigned arg1, unsigned arg2) {
            prog_.emplace_back(type, arg1, arg2);
        }
        void set_next_state(CachedState *next_state) { next_state_ = next_state; }
        void optimize(size_t new_size);

        UpdateEnum type() const { return type_; }
        CachedState *next_state() const { return next_state_; }
        UpdateProg const& prog() const { return prog_; }

        std::string to_str() const;
        std::string DOT_label() const;

        private:
        UpdateEnum type_;
        CachedState *next_state_;
        UpdateProg prog_;
    };

    using UpdateVec = std::vector<Update>;

    struct Guard {
        std::string DOT_label() const;
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
    using GuardedResets = std::vector<std::vector<std::pair<CA::StateId, CA::CounterId>>>;

    class SmallTrans {
        public:
        SmallTrans(GuardVec&& guards) : guards_(std::move(guards)), updates_() {}

        void add_update(Update&& update) { updates_.push_back(std::move(update)); }
        GuardVec const& guards() const { return guards_; }
        Update const& update(unsigned index) const { return updates_[index]; }
        UpdateVec & updates() { return updates_; }

        std::string to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
                std::unordered_map<State, unsigned> &state_ids,
                std::unordered_map<uint8_t, std::string> &byte_dbg) const;

      private:
        GuardVec guards_;
        UpdateVec updates_;
    };

    using UpdateCache = std::unordered_map<uint64_t, Update>;

    class TransBuilder {
        public:
        TransBuilder(unsigned lval_table_size) : 
            normal_(), counter_(), lvals_(lval_table_size), cnts_to_reset_() {}

        TransBuilder(NormalStateVec const& normal, CounterStateVec const& counter,
                LValueTable const& lvals, CountersToReset const& cnts_to_reset) :
            normal_(normal), counter_(counter), lvals_(lvals), cnts_to_reset_(cnts_to_reset) {}

        // do not use again after calling this function
        Update compute_update(CSA& csa);

        void add_normal_state(CA::StateId state) { normal_.insert(state); }
        void add_lval(LValue lval, LValueRowIndex index) {
            lvals_.add_lval(lval.state(), lval.type(), index);
            counter_.insert(CounterState(lval.state()));
        }
        void add_rst(CA::StateId state, CA::CounterId counter) { 
            counter_.insert(CounterState(state));
            cnts_to_reset_.add_state(state, counter); 
        }

        std::string to_str() const;
        std::string to_DOT(uint32_t origin_id) const;

        private:
        OrdVector<OrdVector<CA::StateId>> compute_state_idexes(CSA& csa, Update& update);

        protected:
        NormalStateVec normal_;
        CounterStateVec counter_;
        LValueTable lvals_;
        CountersToReset cnts_to_reset_;
    };

    class GuardedTransBuilder : public TransBuilder {
        public:
        GuardedTransBuilder(CA::CA<uint8_t> const& ca, State const &state,
                uint8_t symbol);

        void add_cnt_state(CA::CA<uint8_t> const& ca, CounterState const &cnt_state,
                uint8_t symbol);
        GuardVec const &guards() const { return guards_; }
        TransEnum trans_type() const;

        // can be called multipletimes
        Update create_update(std::vector<bool> const &sat_guards, CSA &csa);

        // after calling those function the builder must not be used again
        SmallTrans *small(CSA &csa);
        Update *no_condition(CSA &csa);


        private:
        void prepare_builder(std::vector<bool> const& sat_guards, TransBuilder& builder);

        GuardVec guards_;
        GuardedStates guarded_states_;
        GuardedLvals guarded_lvals_;
        GuardedResets guarded_resets_;
    };

    class LazyTrans {
        public:
        LazyTrans(GuardedTransBuilder&& builder) : builder_(std::move(builder)), cache_() {}
        GuardVec const& guards() const { return builder_.guards(); }
        Update const &update(unsigned index,
                             std::vector<bool> const &sat_guards, CSA &csa);

        std::string to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
                std::unordered_map<State, unsigned> &state_ids,
                std::unordered_map<uint8_t, std::string> &byte_dbg) const;

      private:
        GuardedTransBuilder builder_;
        UpdateCache cache_;
    };

    class Trans {
        public:
        Trans() : type_(TransEnum::NotComputed) {}

        void set_next_state(CachedState* next_state, TransEnum type) { type_ = type; next_state_ = next_state; }
        void set_small(SmallTrans* small) { type_ = TransEnum::Small; small_ = small; }
        void set_update(Update* update) { type_ = TransEnum::NoCondition; update_ = update; }
        void set_lazy(LazyTrans* lazy) { type_ = TransEnum::Lazy; lazy_ = lazy; }


        TransEnum type() const { return type_; }

        CachedState* next_state() const { 
            assert(type_ == TransEnum::WithoutCntState || type_ == TransEnum::EnteringCntState);
            return next_state_; 
        }
        Update* update() { assert(type_ == TransEnum::NoCondition); return update_; }
        Update const* update() const { assert(type_ == TransEnum::NoCondition); return update_; }
        SmallTrans* small() { assert(type_ == TransEnum::Small); return small_; }
        LazyTrans* lazy() { assert(type_ == TransEnum::Lazy); return lazy_; }

        std::string to_str() const;

        std::string to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
                std::unordered_map<State, unsigned> &state_ids,
                std::unordered_map<uint8_t, std::string> &byte_dbg) const;

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

        CachedState* get_state(State state);
        CA::CA<uint8_t> const& ca() const { return ca_; }

        // for debugging
        std::string to_str() const;
        std::string to_DOT() const;
        void compute_full();

        private:
        CA::CA<uint8_t> ca_;
        StateCache states_;
    };

    class Config {
        public:
        Config(CA::CA<uint8_t> &&ca)
            : csa_(std::move(ca)), cur_state_(csa_.get_state(InitialState)), 
            init_state_(cur_state_), cnt_sets_(), cnt_sets_tmp_() {}
        Config(Config&&) = delete;
        Config(Config&) = delete;
        Config& operator=(Config&) = delete;
        Config& operator=(Config&&) = delete;

        void reset() { cur_state_ = init_state_; cnt_sets_.resize(0); }
        bool step(uint8_t c); // true if there is still chance to match
        bool accepting();

        std::string csa_to_DOT() const;

        private:
        bool eval_guard(CA::Guard guard, CounterState const& cnt_state);
        void execute_update(Update const& update);
        uint64_t compute_update_index(GuardVec const& guards);
        void compute_trans(Trans& trans, uint8_t byte_class);
        Update const& get_lazy_update(LazyTrans& lazy);

        std::string cnt_sets_to_str() const;

        CSA csa_;
        CachedState* cur_state_;
        CachedState* init_state_;
        CntSetVec cnt_sets_;
        CntSetVec cnt_sets_tmp_;
    };

    class Matcher {
        public:
        Matcher(std::string_view pattern);
        bool match(std::string_view text);

        private:
        Config config_;
    };

    class Visualizer {
        public:
        Visualizer(std::string_view pattern);
        std::string to_DOT_CSA();
        
        std::string to_DOT_CA();

        private:
        std::string pattern_;
        CSA csa_;
    };

} // namespace CSA

