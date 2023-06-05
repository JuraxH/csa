#pragma once

#include <list>
#include <iostream>
#include <unordered_map>

#include "re2/ca.hh"
#include "util/ord_vector.hh"

namespace CSA {

template<typename T>
using OrdVector = Mata::Util::OrdVector<T>;
using CounterIndex = unsigned;
using IndexVec = OrdVector<CounterIndex>;

enum class Errors {
    DOUBLE_INCREMENT = 10,
    INTERNAL_FAIL = 11
};


class CounterState {
    public:
    CounterState() : state_(), normal_(), plus_() {};
    CounterState(CA::StateId state, IndexVec normal, IndexVec plus) :
        state_(state), normal_(normal), plus_(plus) {};

    bool operator<(const CounterState &other) const {
        return state_ < other.state_;
    }

    bool operator==(const CounterState &other) const {
        return state_ == other.state_;
    }

    CA::StateId state() const { return state_; }
    IndexVec & normal() const { return normal_; }
    IndexVec & plus() const { return plus_; }

    std::string toString() const;

    private:

    CA::StateId state_;
    mutable IndexVec normal_;
    mutable IndexVec plus_;
};

using CounterStateVec = OrdVector<CounterState>;
using NormalStateVec = OrdVector<CA::StateId>;

// from boost:
// https://www.boost.org/doc/libs/1_35_0/doc/html/boost/hash_combine_id241013.html
// license: https://www.boost.org/LICENSE_1_0.txt
template<typename T> static void hash_combine(size_t &seed, T const &val) {
    seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

struct Config {
    bool operator==(const Config &other) const {
        return normal_states == other.normal_states
            && counter_states == other.counter_states;
    }

    std::string toString() const;

    NormalStateVec normal_states;
    CounterStateVec counter_states;
    unsigned counter_buffer_size;
};

struct ConfigHasher {
    std::size_t operator()(const Config &config) const {
        size_t seed = 0;
        for (CA::StateId state : config.normal_states) {
            hash_combine(seed, state);
        }
        for (CounterState const& state : config.counter_states) {
            hash_combine(seed, state.state());
            for (CounterIndex const& index : state.normal()) {
                hash_combine(seed, index);
            }
            for (CounterIndex const& index : state.plus()) {
                hash_combine(seed, index);
            }
        }
        return seed;
    }
};

// offset list structure
class CounterSet {
    public:

    CounterSet() : list_(), offset_(0) {}
    CounterSet(unsigned val) :  list_({0,}), offset_(val) { }
    CounterSet(CounterSet &&other) : list_(std::move(other.list_)),
        offset_(other.offset_) {}
    CounterSet(const CounterSet &other) : list_(other.list_),
        offset_(other.offset_) {}

    unsigned offset() const { return offset_; }
    std::list<unsigned> const& list() const { return list_; }

    CounterSet &operator=(CounterSet &&other) {
        offset_ = other.offset_;
        list_ = std::move(other.list_);
        return *this;
    }

    CounterSet &operator=(CounterSet const& other) {
        offset_ = other.offset_;
        list_ = other.list_;
        return *this;
    }

    void merge(CounterSet &&other) {
        std::list<unsigned>::iterator lhs;
        std::list<unsigned>::iterator rhs;
        if (offset_ > other.offset_) {
            lhs = list_.begin();
            rhs = other.list_.begin();
            while (rhs != other.list_.end()) {
                if (lhs == list_.end()) {
                    list_.insert(lhs, offset_ - (other.offset_ - *rhs));
                    ++rhs;
                } else if (offset_ - *lhs > other.offset_ - *rhs) {
                    list_.insert(lhs, offset_ - (other.offset_ - *rhs));
                    ++rhs;
                } else if (offset_ - *lhs < other.offset_ - *rhs) {
                    ++lhs;
                } else {
                    ++lhs; ++rhs;
                }
            }
        } else {
            lhs = other.list_.begin();
            rhs = list_.begin();
            while (rhs != list_.end()) {
                if (lhs == other.list_.end()) {
                    other.list_.insert(lhs, other.offset_ - (offset_ - *rhs));
                    ++rhs;
                } else if (other.offset_ - *lhs > offset_ - *rhs) {
                    other.list_.insert(rhs, other.offset_ - (offset_ - *lhs));
                    ++rhs;
                } else if (other.offset_ - *lhs < offset_ - *rhs) {
                    ++lhs;
                } else {
                    ++lhs; ++rhs;
                }
            }
            list_ = std::move(other.list_);
            offset_ = other.offset_;
        }
    }

    void increment(unsigned max) {
        offset_++;
        if (offset_ - list_.back() > max) {
            list_.pop_back();
        }
    }
    void rst_to_1() {
        offset_ = 1;
        list_.resize(1, 0);
    }

    void insert_1() {
        if (list_.empty()) {
            offset_ = 1;
            list_.push_back(0);
        } else if (offset_ - list_.front() != 1) {
            list_.push_front(offset_ - 1);
        }
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "{";
        for (unsigned i : list_) {
            oss << offset_ - i << ", ";
        }
        oss << "}";
        return oss.str();
    }

    private:

    std::list<unsigned> list_; 
    unsigned offset_;
};

using CounterBuffer = std::vector<CounterSet>;


enum class CntSetInstEnum {
    MOVE,
    INC,
    INSERT1,
    RST_TO_1,
};

enum class LValueType {
    NOOP,
    PLUS,
};

struct LValue {
    CA::StateId state;
    mutable LValueType type;
    bool operator<(const LValue &other) const {
        return this->state < other.state;
    }
    bool operator==(const LValue &other) const {
        return this->state == other.state;
    }
    std::string toString() const;
};

struct LValueTable {
    LValueTable(unsigned size) : tab(size, std::vector<LValue>{}) { }
    void add_lval(LValue lval, unsigned index) {
        std::vector<LValue> &row = tab[index];
        std::vector<LValue>::iterator it = find(row.begin(), row.end(), lval);
        if (it == row.end()) {
            row.push_back(lval);
        } else if (it->type != lval.type) {
            exit(static_cast<int>(Errors::DOUBLE_INCREMENT));
        }
    }

    std::vector<std::vector<LValue>> tab;
};

struct CntSetInst {
    std::string toString() const;

    CntSetInstEnum instruction;
    union {
        struct {
            CA::StateId origin;
            CA::StateId target;
        };
        struct {
            CA::StateId index;
            unsigned max;
        };
    };
};

using UpdateProg = std::vector<CntSetInst>;

struct Trans;

using CachedConfig = std::pair<const Config, std::vector<Trans>>;

enum class UpdateEnum {
    ID,
    KEEP_BUFFER,
    NEW_BUFFER,
    ENTER,
};

// compiled update
struct Update {
    Update (UpdateEnum type, CachedConfig *new_config, UpdateProg &&update_prog) 
        : type(type), new_config(new_config), prog(update_prog) {}
    Update () : type(UpdateEnum::ID), new_config(nullptr), prog() {}

    std::string toString() const;

    UpdateEnum type;
    CachedConfig *new_config;
    UpdateProg prog;
};

struct Guard {
    CA::StateId state;
    CA::Guard condition;
};

struct ResetCounter {
    bool operator==(const ResetCounter &other) const {
        return counter == other.counter;
    }
    bool operator==(const unsigned &other) const {
        return counter == other;
    }
    bool operator<(const ResetCounter &other) const {
        return counter < other.counter;
    }

    CA::CounterId counter;
    OrdVector<CA::StateId> states;
};

struct CountersToReset {
    void add_state(CA::StateId state, CA::CounterId counter) {
        assert(counter != CA::NoCounter);

        std::vector<ResetCounter>::iterator it = find(counters.begin(),
                counters.end(), counter);
        if (it == counters.end()) {
            counters.push_back(ResetCounter{counter, {state}});
        } else {
            it->states.insert(state);
        }
    }

    OrdVector<OrdVector<CA::StateId>> get_counter_indexes() {
        OrdVector<OrdVector<CA::StateId>> res;
        for (auto &rst_counter : counters) {
            res.insert(std::move(rst_counter.states));
        }
        return res;
    }

    std::vector<ResetCounter> counters;
};


struct PartialyComputedTrans {
    PartialyComputedTrans(
            NormalStateVec &&normal_states,
            CounterStateVec &&counter_states,
            LValueTable &&lval_tab,
            CountersToReset &&reset,
            std::vector<Guard> &&guards,
            std::vector<std::vector<CA::StateId>> &&guarded_states,
            std::vector<std::vector<std::pair<LValue, unsigned>>> &&guarded_lvals,
            std::vector<std::vector<CA::StateId>> &&guarded_reset
            ) : 
        normal_states(normal_states),
        counter_states(counter_states),
        lval_tab(lval_tab),
        reset(reset),
        guards(guards),
        guarded_states(guarded_states),
        guarded_lvals(guarded_lvals),
        guarded_reset(guarded_reset),
        updates()
    { }

    NormalStateVec normal_states;
    CounterStateVec counter_states;
    LValueTable lval_tab;
    CountersToReset reset;
    std::vector<Guard> guards;
    std::vector<std::vector<CA::StateId>> guarded_states;
    std::vector<std::vector<std::pair<LValue, unsigned>>> guarded_lvals;
    std::vector<std::vector<CA::StateId>> guarded_reset;
    std::unordered_map<size_t, Update> updates;
};


struct SmallTrans {
    std::vector<Guard> guards;
    std::vector<Update> updates;
};

enum class TransEnum {
    NOT_COMPUTED, // transition for this symbol is not computed yet
    WITHOUT_COUNTER, // does not involve any counter
    SIMPLE, // does no involve any condition
    SMALL, // with only few condition, is fully computed
    LARGE, // lot of condition will be computed only partialy
};

const int small_trans_condition_limit = 2;

struct Trans {
    Trans() : type(TransEnum::NOT_COMPUTED) {}
    ~Trans() {
        switch (type) {
            case TransEnum::NOT_COMPUTED:
            case TransEnum::WITHOUT_COUNTER:
                break;
            case TransEnum::SIMPLE:
                delete update;
                break;
            case TransEnum::SMALL:
                delete small_trans;
                break;
            case TransEnum::LARGE:
                delete partialy_computed_trans;
                break;
        }
    }
    std::string toString() const;

    TransEnum type;
    union {
        struct { // WITHOUT_COUNTER
            std::pair<const Config, std::vector<Trans>> *new_config;
        };
        struct { // SIMPLE
            Update *update;
        };
        struct { // SMALL
            SmallTrans *small_trans;
        };
        struct { // LARGE
            PartialyComputedTrans *partialy_computed_trans;
        };
    };
};

const Config InitConfig = Config{{CA::InitState,}, {}, 0};

class CSA {
    public:

    CSA(std::string const& pattern);

    bool match(const std::string& text);

    void print_counter_buffer() const {
        std::cerr << "counter_buffer: ";
        for (unsigned i = 0; i < counter_buffer.size(); ++i) {
            std::cerr << i << ": "  << counter_buffer[i].toString() << "| ";
        }
        std::cerr << std::endl;
    }

    private:

    CachedConfig *get_next_config(CachedConfig *cur, int byteclass);
    void compute_trans(Trans &trans, CachedConfig *cur, int byteclass);
    Trans& get_trans(CachedConfig *cur, int byte_class);

    bool eval_guard(Guard &guard, Config const& config);
    bool test_final_condition(CachedConfig *cur);

    void execute_update(Update &update, unsigned buffer_size);

    Update& get_update_large(CachedConfig *cur_config,
            PartialyComputedTrans &part_trans);
    void compute_trans_no_counter(Trans &trans, CachedConfig* cur_config,
            CountersToReset&& rst, NormalStateVec&& new_normal_states,
            CounterStateVec&& new_counter_states);
    UpdateEnum get_update_type(std::vector<CntSetInst> &inst_vec,
            unsigned old_buffer_size, unsigned new_buffer_size);
    UpdateEnum build_update(CounterStateVec &new_counter_states,
            LValueTable &lval_tab, CountersToReset &rst, unsigned &buffer_size,
            std::vector<CntSetInst> &inst_vec);

    unsigned get_guard_index(const Config &config, std::vector<Guard> &guards);
    void build_simple_trans(Trans &trans, CachedConfig *cur_config,
            NormalStateVec &new_normal_states,
            CounterStateVec &new_counter_states, LValueTable &lval_tab,
            CountersToReset &rst);
    void build_small_trans(Trans &trans, CachedConfig *cur_config,
            NormalStateVec &normal_states, CounterStateVec &counter_states,
            LValueTable &lval_tab, CountersToReset &rst,
            std::vector<std::vector<std::pair<LValue, unsigned>>> &guarded_lvals,
            std::vector<std::vector<CA::StateId>> &guarded_reset,
            std::vector<std::vector<CA::StateId>> &guarded_states,
            std::vector<Guard> &&guards);
    Update get_update_for_evaluation(std::vector<bool> &sat_guards, 
            CachedConfig *cur_config, NormalStateVec normal_states,
            CounterStateVec counter_states, LValueTable lval_tab,
            CountersToReset rst,
            std::vector<std::vector<std::pair<LValue, unsigned>>> &guarded_lvals,
            std::vector<std::vector<CA::StateId>> &guarded_reset,
            std::vector<std::vector<CA::StateId>> &guarded_states);

    using ConfigCache = std::unordered_map<
            Config, std::vector<Trans>, ConfigHasher>;

    CA::CA ca;
    ConfigCache configs;
    CounterBuffer counter_buffer;

};
    
}
