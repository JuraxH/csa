#include "csa.hh"
#include "re2/ca.hh"
#include "re2/glushkov.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <map>

namespace CSA {

using namespace std;
using namespace std::string_literals;


unsigned CountingSet::max_postponed(int max) const {
    assert(!list_.empty());

    if (static_cast<int>(offset_ - list_.back() + 1) <= max || max == -1) {
        return offset_ - list_.back() + 1;
    }
    return offset_ - *prev(list_.end(), 2) + 1;
}

unsigned CountingSet::min_postponed() const {
    assert(!list_.empty());

    return offset_ - list_.front() + 1;
}

void CountingSet::merge(CountingSet &&other) {
    assert(!other.list_.empty());

    if (list_.empty()) {
        list_ = std::move(other.list_);
        offset_ = other.offset_;
        return;
    }
    if (other.offset_ - other.list_.back() > offset_ - list_.back()) {
        list_.swap(other.list_);
        std::swap(offset_, other.offset_);
        return;
    }
    auto dst = list_.begin();
    auto src = other.list_.begin();
    while (src != other.list_.end()) {
        if (other.offset_ - *src < offset_ - *dst) {
            list_.insert(dst, offset_ - (other.offset_ - *src));
        } else if (other.offset_ - *src > offset_ - *dst) {
            dst++;
        } else {
            dst++;
            src++;
        }
    }
}

void CountingSet::increment(int max) {
    assert(!list_.empty());

    offset_++;
    if (static_cast<int>(offset_ - list_.back()) > max && max != -1) {
        list_.pop_back();
    }
}

void CountingSet::rst_to_1() {
    offset_ = 1;
    list_.clear();
    list_.push_back(0);
}

void CountingSet::insert_1() {
    if (list_.empty() || list_.front() != offset_ - 1) {
        list_.push_front(offset_ - 1);
    }
}

vector<unsigned> CountingSet::to_vec() const {
    vector<unsigned> vec;
    vec.reserve(list_.size());
    for (auto i : list_) {
        vec.push_back(offset_ - i);
    }
    return vec;
}

void LValueTable::add_lval(CA::StateId state, LValueEnum type, LValueRowIndex index) {
    std::vector<LValue> &row = tab_[index];
    std::vector<LValue>::iterator it = find(row.begin(), row.end(), LValue(state, type));
    if (it == row.end()) {
        row.push_back(LValue(state, type));
    } else if (it->type() != type) {
        FATAL_ERROR("lvalue both actual and postponed", Errors::DoubleIncr);
    }
}

void CountersToReset::add_state(CA::StateId state, CA::CounterId counter) {
    auto it = counters.find(CounterToReset(counter));
    if (it != counters.end()) {
        it->add_state(state);
    } else {
        counters.insert(CounterToReset(counter, state));
    }
}

OrdVector<OrdVector<CA::StateId>> CountersToReset::get_cnt_set_names() {
    OrdVector<OrdVector<CA::StateId>> names;
    for (auto& cnt : counters) {
        names.insert(cnt.get_states());
    }
    return names;
}

void Update::optimize(size_t new_size) {
    if (new_size == 0) {
        type_ = UpdateEnum::Out;
        return;
    }
    std::vector<bool> moved_to(new_size, false);
    // checks if all moves are only renames
    for (auto &inst : prog_) {
        if (inst.type() == CntSetInstEnum::Move) {
            moved_to[inst.target()] = true;
            if (inst.target() != inst.origin()) {
                type_ = UpdateEnum::NewSets;
                return;
            }
        }
    }
    vector<CntSetInst> new_prog;
    for (auto &inst: prog_) {
        if (inst.type() == CntSetInstEnum::Insert_1 && !moved_to[inst.target()]) {
            new_prog.emplace_back(CntSetInstEnum::Rst_to_1, inst.origin(), inst.target());
        } else if (inst.type() != CntSetInstEnum::Move) {
            new_prog.push_back(inst);
        }
    }
    prog_ = std::move(new_prog);
    if (prog_.empty()) {
        type_ = UpdateEnum::Noop;
    } else {
        type_ = UpdateEnum::KeepSets;
    }
}

OrdVector<OrdVector<CA::StateId>> TransBuilder::compute_state_idexes(CSA& csa, Update& update) {
    OrdVector<OrdVector<CA::StateId>> state_indexes{};
    for (unsigned i = 0; i < lvals_.size(); ++i) {
        auto & row = lvals_[i];
        if (row.empty()) {
            continue;
        }
        bool all_plus = true;
        OrdVector<CA::StateId> states;
        for (auto lval : row) {
            if (lval.type() != LValueEnum::Plus) {
                all_plus = false;
            }
            states.insert(lval.state());
        }
        if (all_plus) {
            auto max = csa.ca().get_counter(csa.ca().get_state(row[0].state()).cnt()).max();
            update.add_inst(CntSetInstEnum::Incr, i, max);
            for (auto &lval : row) {
                lval.set_type(LValueEnum::ID);
            }
        }
        state_indexes.insert(std::move(states));
    }
    return state_indexes;
}

Update TransBuilder::compute_update(CSA& csa) {
    LOG_BUILDER(to_str());
    Update update;
    auto state_indexes = compute_state_idexes(csa, update);
    auto rst_cnt_names = cnts_to_reset_.get_cnt_set_names();
    for (auto& name: rst_cnt_names) {
        state_indexes.insert(std::move(name));
    }
    for (unsigned i = 0; i < lvals_.size(); i++) {
        auto& row = lvals_[i];
        if (row.empty()) {
            continue;
        }
        OrdVector<CA::StateId> states;
        for (auto lval : row) { states.insert(lval.state()); }
        unsigned index = state_indexes.get_index(states);
        update.add_inst(CntSetInstEnum::Move, i, index);
        for (auto lval : row) {
            if (lval.type() == LValueEnum::ID) {
                counter_.find(CounterState(lval.state()))->actual().insert(index);
            } else {
                counter_.find(CounterState(lval.state()))->postponed().insert(index);
            }
        }
    }
    for (auto& name: rst_cnt_names) {
        unsigned index = state_indexes.get_index(name);
        update.add_inst(CntSetInstEnum::Insert_1, index, index);
        for (auto state : name) {
            counter_.find(CounterState(state))->actual().insert(index);
        }
    }
    update.optimize(state_indexes.size());
    State state(std::move(normal_), std::move(counter_), state_indexes.size());
    update.set_next_state(csa.get_state(std::move(state)));
    return update;
}

GuardedTransBuilder::GuardedTransBuilder(CA::CA<uint8_t> const& ca,
        State const& state, uint8_t symbol) : TransBuilder(state.cnt_sets()),
        guards_(), guarded_states_(), guarded_lvals_(), guarded_resets_() {
    for (auto const& ca_state : state.normal()) {
        auto& ca_transitions = ca.get_state(ca_state).transitions();
        for (auto& ca_trans : ca_transitions) {
            if (ca_trans.symbol() != symbol && ca_trans.symbol() != ca.bytemap_range()) {
                continue;
            }
            auto target = ca_trans.target();
            if (ca_trans.op() == CA::Operator::Rst) {
                counter_.insert(CounterState(target));
                cnts_to_reset_.add_state(target, ca.get_state(target).cnt());
            } else {
                normal_.insert(target);
            }
        }
    }
    for (auto const& cnt_state : state.counter()) {
        add_cnt_state(ca, cnt_state, symbol);
    }
}

void GuardedTransBuilder::add_cnt_state(CA::CA<uint8_t> const& ca,
        CounterState const &cnt_state, uint8_t symbol) {
    auto ca_state = cnt_state.state();
    auto& ca_transitions = ca.get_state(ca_state).transitions();
    int can_incr = -1;
    int can_exit = -1;
    for (auto& ca_trans : ca_transitions) {
        if (ca_trans.symbol() != symbol && ca_trans.symbol() != ca.bytemap_range()) {
            continue;
        }
        auto target = ca_trans.target();
        if (ca_trans.grd() == CA::Guard::CanIncr) {
            if (can_incr == -1) {
                can_incr = guards_.size();
                guards_.push_back(Guard{ca_state, CA::Guard::CanIncr});
                guarded_lvals_.resize(guards_.size());
                guarded_states_.resize(guards_.size());
                guarded_resets_.resize(guards_.size());
            }
            for (auto index : cnt_state.actual()) {
                guarded_lvals_[can_incr].push_back(
                        make_pair(LValue(target, LValueEnum::Plus), index));
            }
            if (!cnt_state.postponed().empty()) {
                FATAL_ERROR("double increment", Errors::DoubleIncr);
            }
            assert(cnt_state.postponed().empty());
        } else if (ca_trans.grd() == CA::Guard::CanExit) {
            if (can_exit == -1) {
                can_exit = guards_.size();
                guards_.push_back(Guard{ca_state, CA::Guard::CanExit});
                guarded_lvals_.resize(guards_.size());
                guarded_states_.resize(guards_.size());
                guarded_resets_.resize(guards_.size());
            }
            if (ca_trans.op() == CA::Operator::Rst) {
                guarded_resets_[can_exit].push_back(
                        make_pair(target, ca.get_state(target).cnt()));
            } else {
                guarded_states_[can_exit].push_back(target);
            }
        } else {
            switch(ca_trans.op()) {
                case CA::Operator::Incr:
                    counter_.insert(CounterState(target));
                    for (auto index : cnt_state.actual()) {
                        lvals_.add_lval(target, LValueEnum::Plus, index);
                    }
                    assert(cnt_state.postponed().empty());
                    break;
                case CA::Operator::Rst:
                    counter_.insert(CounterState(target));
                    cnts_to_reset_.add_state(target, ca.get_state(target).cnt());
                    break;
                case CA::Operator::ID:
                    counter_.insert(CounterState(target));
                    for (auto index : cnt_state.actual()) {
                        lvals_.add_lval(target, LValueEnum::ID, index);
                    }
                    for (auto index : cnt_state.postponed()) {
                        lvals_.add_lval(target , LValueEnum::Plus, index);
                    }
                    break;
                case CA::Operator::Noop:
                    normal_.insert(target);
                    break;
            }
        }
    }
}

TransEnum GuardedTransBuilder::trans_type() const {
    if (guards_.size() == 0) {
        return TransEnum::NoCondition;
    } else if (guards_.size() <= max_conditions_in_small_trans) {
        return TransEnum::Small;
    } else {
        return TransEnum::Lazy;
    }
}

Update GuardedTransBuilder::create_update(std::vector<bool> const& sat_guards, CSA& csa) {
    // creating copy because the computation destroys the builder
    TransBuilder builder(normal_, counter_, lvals_, cnts_to_reset_);
    prepare_builder(sat_guards, builder);
    return builder.compute_update(csa);
}

// ugly :( 
const std::vector<bool> small_size_1_1{false,};
const std::vector<bool> small_size_1_2{true,};
const std::vector<bool> small_size_2_1{false, false};
const std::vector<bool> small_size_2_2{true, false};
const std::vector<bool> small_size_2_3{false, true};
const std::vector<bool> small_size_2_4{true, true};

SmallTrans* GuardedTransBuilder::small(CSA& csa) {
    auto grds = guards_.size();
    auto* trans = new SmallTrans(std::move(guards_));
    if (grds == 1) {
        TransBuilder builder(normal_, counter_, lvals_, cnts_to_reset_);
        prepare_builder(small_size_1_1, builder);
        trans->add_update(builder.compute_update(csa));

        builder = TransBuilder(std::move(normal_), std::move(counter_),
                               std::move(lvals_), std::move(cnts_to_reset_));
        prepare_builder(small_size_1_2, builder);
        trans->add_update(builder.compute_update(csa));
    } else {
        TransBuilder builder(normal_, counter_, lvals_, cnts_to_reset_);
        prepare_builder(small_size_2_1, builder);
        trans->add_update(builder.compute_update(csa));
        builder = TransBuilder(normal_, counter_, lvals_, cnts_to_reset_);
        prepare_builder(small_size_2_2, builder);
        trans->add_update(builder.compute_update(csa));
        builder = TransBuilder(normal_, counter_, lvals_, cnts_to_reset_);
        prepare_builder(small_size_2_3, builder);
        trans->add_update(builder.compute_update(csa));
        builder = TransBuilder(std::move(normal_), std::move(counter_),
                               std::move(lvals_), std::move(cnts_to_reset_));
        prepare_builder(small_size_2_4, builder);
        trans->add_update(builder.compute_update(csa));
    }
    return trans;
}

Update* GuardedTransBuilder::no_condition(CSA& csa) {
    return new Update(compute_update(csa));
}

void GuardedTransBuilder::prepare_builder(std::vector<bool> const& sat_guards,
        TransBuilder& builder) {
    for (size_t i = 0; i < sat_guards.size(); i++) {
        if (sat_guards[i]) {
            for (auto state : guarded_states_[i]) {
                builder.add_normal_state(state);
            }
            for (auto lval : guarded_lvals_[i]) {
                builder.add_lval(lval.first, lval.second);
            }
            for (auto guarded_resets_ : guarded_resets_[i]) {
                builder.add_rst(guarded_resets_.first, guarded_resets_.second);
            }
        }
    }
}

Update const& LazyTrans::update(unsigned index, vector<bool> const& sat_guards, CSA& csa) {
    auto it = cache_.find(index);
    if (it != cache_.end()) {
        return it->second;
    } else {
        it = cache_.insert(it, make_pair(index, builder_.create_update(sat_guards, csa)));
        return it->second;
    }
}

Trans::~Trans() {
    switch (type_) {
        case TransEnum::NoCondition:
            delete update_;
            break;
        case TransEnum::Small:
            delete small_;
            break;
        case TransEnum::Lazy:
            delete lazy_;
            break;
        default:
            break;
    }
}

CachedState* CSA::get_state(State state) {
    auto it = states_.find(state);
    if (it != states_.end()) {
        return &(*it);
    } else {
        it = states_.insert(it, make_pair(state, vector<Trans>(ca_.bytemap_range())));
        return &(*it);
    }
}

bool Config::step(uint8_t c) {
    LOG_CONFIG(cur_state_->first.to_str(), cnt_sets_to_str());
    LOG_CONFIG_SYMBOL(((c >= '!' && c <= '~') ? ("\""s + string(1, c) + "\""s) : to_string(c)), to_string(csa_.ca().get_byte_class(c)));
    uint8_t byte_class = csa_.ca().get_byte_class(c);
    Trans& trans = cur_state_->second[byte_class];
    if (trans.type() == TransEnum::NotComputed) {
        compute_trans(trans, byte_class);
    }
    switch(trans.type()) {
        case TransEnum::WithoutCntState:
            cur_state_ = trans.next_state();
            break;
        case TransEnum::EnteringCntState:
            cur_state_ = trans.next_state();
            cnt_sets_.resize(cur_state_->first.cnt_sets(), CountingSet(1));
            break;
        case TransEnum::NoCondition:
            execute_update(*trans.update());
            break;
        case TransEnum::Small:
            execute_update(trans.small()->update(
                compute_update_index(trans.small()->guards())));
            break;
        case TransEnum::Lazy:
            execute_update(get_lazy_update(*trans.lazy()));
            break;
        default:
            FATAL_ERROR("unexpected type of transition", Errors::InternalFailure);
    }
    if (cur_state_->first.normal().empty() && cur_state_->first.counter().empty()) {
        return false;
    }
    return true;
}

bool Config::accepting() {
    for (auto state : cur_state_->first.normal()) {
        if (csa_.ca().get_state(state).final() == CA::Guard::True) {
            return true;
        }
    }
    for (auto& state : cur_state_->first.counter()) {
        auto guard = csa_.ca().get_state(state.state()).final();
        if (guard == CA::Guard::True) {
            return true;
        } else if (guard == CA::Guard::CanExit) {
            if (eval_guard(guard, state)) {
                return true;
            }
        }
    }
    return false;
}

bool Config::eval_guard(CA::Guard guard, CounterState const& cnt_state) {
    LOG_EVAL_GUARD(CA::guard_to_string(guard), cnt_state.to_str());
    if (guard == CA::Guard::CanIncr) {
        auto max = csa_.ca().get_counter(csa_.ca().get_state(cnt_state.state()).cnt()).max();
        LOG_EVAL_GUARD_MAX(max);
        for (auto i : cnt_state.actual()) {
            if (cnt_sets_[i].min() < static_cast<unsigned>(max)) {
                LOG_EVAL_GUARD_RES("true");
                return true;
            }
        }
        if (!cnt_state.postponed().empty()) {
            FATAL_ERROR("testing guard on state that has postponed incr", Errors::DoubleIncr);
        }
        LOG_EVAL_GUARD_RES("false");
        return false;
    } else { // CanExit
        auto min = csa_.ca().get_counter(csa_.ca().get_state(cnt_state.state()).cnt()).min();
        LOG_EVAL_GUARD_MIN(min);
        for (auto i : cnt_state.actual()) {
            if (cnt_sets_[i].max() >= static_cast<unsigned>(min)) {
                LOG_EVAL_GUARD_RES("true");
                return true;
            }
        }
        if (!cnt_state.postponed().empty()) {
            FATAL_ERROR("testing guard on state that has postponed incr", Errors::DoubleIncr);
        }
        LOG_EVAL_GUARD_RES("false");
        return false;
    }
}

void Config::execute_update(Update const& update) {
    LOG_UPDATE(update.to_str());
    switch(update.type()) {
        case UpdateEnum::Noop:
            break;
        case UpdateEnum::Out:
            cnt_sets_.resize(0);
            break;
        case UpdateEnum::KeepSets:
            cnt_sets_.resize(update.next_state()->first.cnt_sets());
            for (auto const& inst : update.prog()) {
                switch (inst.type()) {
                    case CntSetInstEnum::Rst_to_1:
                        cnt_sets_[inst.target()].rst_to_1();
                        break;
                    case CntSetInstEnum::Insert_1:
                        cnt_sets_[inst.target()].insert_1();
                        break;
                    case CntSetInstEnum::Move:
                        FATAL_ERROR("move inst in KeepSets prog", Errors::InternalFailure);
                    case CntSetInstEnum::Incr:
                        cnt_sets_[inst.index()].increment(inst.max());
                        break;
                }
            }
            break;
        case UpdateEnum::NewSets:
            cnt_sets_tmp_.resize(update.next_state()->first.cnt_sets());
            for (auto const& inst : update.prog()) {
                switch (inst.type()) {
                    case CntSetInstEnum::Rst_to_1:
                        FATAL_ERROR("Rst_to_1 in NewSets", Errors::InternalFailure);
                    case CntSetInstEnum::Insert_1:
                        cnt_sets_tmp_[inst.target()].insert_1();
                        break;
                    case CntSetInstEnum::Move:
                        cnt_sets_tmp_[inst.target()].merge(std::move(cnt_sets_[inst.origin()]));
                        break;
                    case CntSetInstEnum::Incr:
                        cnt_sets_[inst.index()].increment(inst.max());
                        break;
                }
            }
            cnt_sets_.swap(cnt_sets_tmp_);
            cnt_sets_tmp_.clear();
    }
    cur_state_ = update.next_state();
}

uint64_t Config::compute_update_index(GuardVec const& guards) {
    uint64_t index = 0;
    for (unsigned i = 0; i < guards.size(); i++) {
        if (eval_guard(guards[i].condition,
                       *cur_state_->first.counter().find(
                           CounterState(guards[i].state)))) {
            index |= (1 << i);
        }
    }
    return index;
}

void Config::compute_trans(Trans& trans, uint8_t byte_class) {
    if (cur_state_->first.counter().empty()) {
        NormalStateVec normal;
        CounterStateVec counter;
        CountersToReset reset;
        for (auto ca_state : cur_state_->first.normal()) {
            auto& ca_transitions = csa_.ca().get_state(ca_state).transitions();
            for (auto& ca_trans : ca_transitions) {
                if (ca_trans.symbol() != byte_class 
                        && ca_trans.symbol() != csa_.ca().bytemap_range()) {
                    continue;
                }
                if (ca_trans.op() == CA::Operator::Noop) {
                    normal.insert(ca_trans.target());
                } else {
                    assert(ca_trans.op() == CA::Operator::Rst);
                    counter.insert(CounterState(ca_trans.target()));
                    reset.add_state(ca_trans.target(), 
                            csa_.ca().get_state(ca_trans.target()).cnt());
                }
            }
        }
        if (counter.empty()) {
            State state(std::move(normal), std::move(counter), 0);
            trans.set_next_state(csa_.get_state(std::move(state)), TransEnum::WithoutCntState);
        } else {
            auto names = reset.get_cnt_set_names();
            auto it = names.begin();
            for (unsigned i = 0; i < names.size(); ++i, ++it) {
                for (auto state : *it) {
                    counter.find(CounterState(state))->actual().insert(i);
                }
            }
            State state(std::move(normal), std::move(counter), names.size());
            trans.set_next_state(csa_.get_state(std::move(state)), TransEnum::EnteringCntState);
        }
    } else {
        GuardedTransBuilder builder(csa_.ca(), cur_state_->first, byte_class);
        switch (builder.trans_type()) {
            case TransEnum::NoCondition:
                trans.set_update(builder.no_condition(csa_));
                break;
            case TransEnum::Small:
                trans.set_small(builder.small(csa_));
                break;
            case TransEnum::Lazy:
                trans.set_lazy(new LazyTrans(std::move(builder)));
                break;
            default:
                FATAL_ERROR("unexpected trans type of builder", Errors::InternalFailure);
        }
    }
}

Update const& Config::get_lazy_update(LazyTrans& lazy) {
    auto const& guards = lazy.guards();
    vector<bool> sat_guards(guards.size(), false);
    uint64_t index = 0;
    for (unsigned i = 0; i < guards.size(); i++) {
        if (eval_guard(guards[i].condition,
                       *cur_state_->first.counter().find(
                           CounterState(guards[i].state)))) {
            index |= (1 << i);
            sat_guards[i] = true;
        }
    }
    return lazy.update(index, sat_guards, csa_);
}

Matcher::Matcher(std::string_view pattern) : config_(CA::glushkov::Builder::get_ca(pattern)) { }

bool Matcher::match(string_view text) {
    for (char c : text) {
        if (!config_.step(c)) {
            config_.reset();
            return false;
        }
    }
    bool res = config_.accepting();
    config_.reset();
    return res;
}

// for debugging:

string CounterState::to_str() const {
    string str = "[S:"s + to_string(state_) + " {"s;
    for (auto i : actual_) {
        str += std::to_string(i) + ", "s;
    }
    str += "} +{"s;
    for (auto i : postponed_) {
        str += std::to_string(i) + ", "s;
    }
    return str + "}]"s;
}

string State::to_str() const {
    string str = "Normal: {"s;
    for (auto const&i : normal_) {
        str += std::to_string(i) + ", "s;
    }
    str += "}\n Counter:\n"s;
    for (auto const&i : counter_) {
        str += i.to_str() + "\n"s;
    }
    return str;
}

string CountingSet::to_str() const {
    string str = "{"s;
    for (auto i : list_) {
        str += std::to_string(offset_ - i) + ", "s;
    }
    return str + "}"s;
}

string LValue::to_str() const {
    if (type_ == LValueEnum::Plus) {
        return std::to_string(state_) + "+"s;
    }
    return to_string(state_);
}

string LValueTable::to_str() const {
    string str = "lval table:\n"s;
    for (size_t i = 0; i < tab_.size(); i++) {
        str += std::to_string(i) + ": "s;
        for (auto const&j : tab_[i]) {
            str += j.to_str() + ", "s;
        }
        str += "\n"s;
    }
    return str;
}

string CounterToReset::to_str() const {
    string str = "[C: "s + std::to_string(counter) + " {"s;
    for (auto state : states) {
        str += "{"s + std::to_string(state) + "}, "s;
    }
    return str + "}]"s;
}

string CountersToReset::to_str() const {
    string str = "{"s;
    for (auto const&i : counters) {
        str += i.to_str() + ", "s;
    }
    return str + "}"s;
}

string CntSetInst::to_str() const {
    switch (type_) {
        case CntSetInstEnum::Move:
            return "MOV "s + to_string(origin_) + " "s + to_string(target_);
        case CntSetInstEnum::Incr:
            return "INC "s + std::to_string(index_) + " (max: "s + std::to_string(max_) + ")"s;
        case CntSetInstEnum::Insert_1:
            return "INSERT_1 "s + to_string(origin_) + " "s + to_string(target_);
        case CntSetInstEnum::Rst_to_1:
            return "RST_TO_1 "s + to_string(origin_);
    }
    return "";
}

string Update::to_str() const {
    string str;
    switch(type_) {
        case UpdateEnum::Out:
            return "OUT next:"s + next_state_->first.to_str();
        case UpdateEnum::Noop:
            return "NOOP next:"s + next_state_->first.to_str();
        case UpdateEnum::KeepSets:
            str += "KeepSets:\n"s;
            break;
        case UpdateEnum::NewSets:
            str += "NewSets:\n"s;
            break;
    }
    for (auto const&inst : prog_) {
        str += '\t' + inst.to_str() + '\n';
    }
    return str + "next:"s + next_state_->first.to_str();
}

string TransBuilder::to_str() const {
    string str = "Normal: {"s;
    for (auto const&i : normal_) {
        str += std::to_string(i) + ", "s;
    }
    str += "} Counter: "s;
    for (auto const&i : counter_) {
        str += to_string(i.state()) + ", "s;
    }
    str += lvals_.to_str();
    str += cnts_to_reset_.to_str();
    return str;
}

string Config::cnt_sets_to_str() const {
    if (cnt_sets_.empty()) {
        return "No cnt set\n"s;
    }
    string str;
    for (size_t i = 0; i < cnt_sets_.size(); ++i) {
        str += std::to_string(i) + ": "s + cnt_sets_[i].to_str() + "\n"s;
    }
    return str;
}

std::string State::DOT_label(CSA const& csa) const {
    string str = "N: "s;
    map<unsigned, string> cnts;
    for (auto state : normal_) {
        str += to_string(state) + ", "s;
    }
    str += "| C: "s;
    for (auto const& cnt_state : counter_) {
        auto cnt = csa.ca().get_state(cnt_state.state()).cnt();
        auto state = to_string(cnt_state.state());
        str += state + ", "s;
        for (auto index : cnt_state.actual()) {
            if (cnts.find(index) == cnts.end()) {
                cnts[index] += "C_"s + to_string(cnt) + ";"s + to_string(index) + ": "s + state;
            } else {
                cnts[index] += ", "s + state;
            }
        }
        for (auto index : cnt_state.postponed()) {
            if (cnts.find(index) == cnts.end()) {
                cnts[index] += "C_"s + to_string(cnt) + ";"s + to_string(index) + ": "s + state + "+"s;
            } else {
                cnts[index] += ", "s + state + "+"s;
            }
        }
    }
    for (auto& [key, val] : cnts) {
        (void)key;
        str += "["s + val + "]"s;
    }

    str += "\\nF: "s;
    // TODO: final condition
    // first traverse normal states if true is found, then end
    for (auto const& state : normal_) {
        if (csa.ca().get_state(state).final() == CA::Guard::True) {
            return str + "True"s;
        }
    }
    // traverse counter states if any condition is found add it to final
    std::string conditions;
    for (auto const& state : counter_) {
        auto const& ca_state = csa.ca().get_state(state.state());
        if (ca_state.final() == CA::Guard::True) {
            return str + "True"s;
        } else if (ca_state.final() == CA::Guard::CanExit) {
            conditions += "["s + to_string(state.state()) + "] >= "s + to_string(csa.ca().get_counter(ca_state.cnt()).max())+ ", "s;
        }
    }
    if (!conditions.empty()) {
        return str + conditions;
    } 
    // else false
    return str + "False"s;
}

string Update::DOT_label() const {
    string str;
    switch(type_) {
        case UpdateEnum::Out:
            return "OUT"s;
        case UpdateEnum::Noop:
            return "NOOP"s;
        case UpdateEnum::KeepSets:
            str += "KeepSets:\n"s;
            break;
        case UpdateEnum::NewSets:
            str += "NewSets:\n"s;
            break;
    }
    for (auto const&inst : prog_) {
        str += '\t' + inst.to_str() + '\n';
    }
    return str;
}

string Trans::to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
                     std::unordered_map<State, unsigned> &state_ids,
                     std::unordered_map<uint8_t, std::string> &byte_dbg) const {
    switch(type_) {
        case TransEnum::WithoutCntState:
        case TransEnum::EnteringCntState:
            {
                if (next_state()->first.dead()) { return ""s; }
                unsigned id;
                if (state_ids.contains(next_state()->first)) {
                    id = state_ids[next_state()->first];
                } else {
                    state_ids[next_state()->first] = id_cnt;
                    id = id_cnt;
                    id_cnt++;
                }
                return to_string(origin_id) + " -> " + to_string(id) + "[label=\"" + byte_dbg[symbol] + "\"]\n";
            }
        case TransEnum::NoCondition:
            {
                if (update()->next_state()->first.dead()) { return ""s; }
                unsigned id;
                if (state_ids.contains(update()->next_state()->first)) {
                    id = state_ids[update()->next_state()->first];
                } else {
                    state_ids[update()->next_state()->first] = id_cnt;
                    id = id_cnt;
                    id_cnt++;
                }
                return to_string(origin_id) + " -> " + to_string(id) + "[label=\"" + byte_dbg[symbol] + "|" + update()->DOT_label() + "\"]\n";
            }
        case TransEnum::Small:
            return this->small_->to_DOT(symbol, origin_id, id_cnt, state_ids, byte_dbg);
        case TransEnum::Lazy:
            return this->lazy_->to_DOT(symbol, origin_id, id_cnt, state_ids, byte_dbg);
            assert(false);
        case TransEnum::NotComputed:
            break;
    }
    return ""s;
}

std::string LazyTrans::to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
        std::unordered_map<State, unsigned> &state_ids,
        std::unordered_map<uint8_t, std::string> &byte_dbg) const {
    std::vector<uint32_t> eval(guards().size(), 0);
    std::string graph;
    for (auto const& [key, val] : cache_) {
        if (val.next_state()->first.dead()) { continue; }
        for (size_t j = 0; j < guards().size(); j++) {
            size_t d = guards().size() - j - 1; // the order of bits is descending
            if (key & (1<<d)) {
                eval[j] = 1;
            } else {
                eval[j] = 0;
            }
        }
        Update const& update = val;
        unsigned target_id;
        if (state_ids.contains(update.next_state()->first)) {
            target_id = state_ids[update.next_state()->first];
        } else {
            state_ids[update.next_state()->first] = id_cnt;
            target_id = id_cnt;
            id_cnt++;
        }
        graph += to_string(origin_id) + " -> " + to_string(target_id) + "[label=\"" + byte_dbg[symbol] + "|";
        for (size_t j = 0; j < guards().size(); j++) {
            graph += guard_to_string(guards()[j].condition) 
                + "["s + to_string(guards()[j].state) + "]:"s;
            if (eval[j]) {
                graph += "T, "s;
            } else {
                graph += "F, "s;
            }
        }
        graph += "|"s + update.DOT_label() + "\"]\n";
    }
    return graph;
}

std::string SmallTrans::to_DOT(uint8_t symbol, uint32_t origin_id, unsigned &id_cnt,
        std::unordered_map<State, unsigned> &state_ids, 
        std::unordered_map<uint8_t, std::string> &byte_dbg) const {
    std::vector<uint32_t> eval(guards_.size(), 0);
    std::string graph;
    for (size_t i = 0; i < ((size_t)1<<guards_.size()); i++) {
        if (updates_[i].next_state()->first.dead()) { continue; }
        for (size_t j = 0; j < guards_.size(); j++) {
            size_t d = guards_.size() - j - 1; // the order of bits is descending
            if (i & (1<<d)) {
                eval[j] = 1;
            } else {
                eval[j] = 0;
            }
        }
        Update const& update = updates_[i];
        unsigned target_id;
        if (state_ids.contains(update.next_state()->first)) {
            target_id = state_ids[update.next_state()->first];
        } else {
            state_ids[update.next_state()->first] = id_cnt;
            target_id = id_cnt;
            id_cnt++;
        }
        graph += to_string(origin_id) + " -> " + to_string(target_id) + "[label=\"" + byte_dbg[symbol] + "|";
        for (size_t j = 0; j < guards_.size(); j++) {
            graph += guard_to_string(guards_[j].condition) 
                  + "["s + to_string(guards_[j].state) + "]:"s;
            if (eval[j]) {
                graph += "T, "s;
            } else {
                graph += "F, "s;
            }
        }
        graph += "|"s + update.DOT_label() + "\"]\n";
    }
    return graph;
}

std::string CSA::to_DOT() const {
    std::unordered_map<uint8_t, std::string> byte_dbg = ca_.bytemap_debug();
    string str = "digraph CSA {\n"s;
    unsigned id_cnt{0};
    std::unordered_map<State, unsigned> state_ids;
    for (auto & state : states_) {
        if (state.first.dead()) { continue; }
        unsigned id;
        if (state_ids.contains(state.first)) {
            id = state_ids[state.first];
        } else {
            state_ids[state.first] = id_cnt;
            id = id_cnt;
            id_cnt++;
        }
        str += to_string(id) + "[shape=\"box\", label=\""s + state.first.DOT_label(*this) + "\"]\n"s;
        for (unsigned i = 0; i < state.second.size(); i++) {
            str += state.second[i].to_DOT(i, id, id_cnt, state_ids, byte_dbg);
        }
    }
    str += "start[style=invis]\nstart -> "s + to_string(state_ids[InitialState]) + "[color=\"red\"]\n"s;
    return str + "}\n"s;
}

std::string Config::csa_to_DOT() const {
    return csa_.to_DOT();
}

class CSATraverseState {
    public:
    CSATraverseState(CSA & csa) : to_visit(), visited(), cur_state_(nullptr), csa_(csa) {
        to_visit.insert(csa.get_state(InitialState)->first);
    }

    bool keep_going() const { return !to_visit.empty(); }
    void compute_full_trans(Trans& trans, uint8_t byte_class) {
        if (cur_state_->first.counter().empty()) {
            NormalStateVec normal;
            CounterStateVec counter;
            CountersToReset reset;
            for (auto ca_state : cur_state_->first.normal()) {
                auto& ca_transitions = csa_.ca().get_state(ca_state).transitions();
                for (auto& ca_trans : ca_transitions) {
                    if (ca_trans.symbol() != byte_class 
                            && ca_trans.symbol() != csa_.ca().bytemap_range()) {
                        continue;
                    }
                    if (ca_trans.op() == CA::Operator::Noop) {
                        normal.insert(ca_trans.target());
                    } else {
                        assert(ca_trans.op() == CA::Operator::Rst);
                        counter.insert(CounterState(ca_trans.target()));
                        reset.add_state(ca_trans.target(), 
                                csa_.ca().get_state(ca_trans.target()).cnt());
                    }
                }
            }
            if (counter.empty()) {
                State state(std::move(normal), std::move(counter), 0);
                trans.set_next_state(csa_.get_state(std::move(state)), TransEnum::WithoutCntState);
            } else {
                auto names = reset.get_cnt_set_names();
                auto it = names.begin();
                for (unsigned i = 0; i < names.size(); ++i, ++it) {
                    for (auto state : *it) {
                        counter.find(CounterState(state))->actual().insert(i);
                    }
                }
                State state(std::move(normal), std::move(counter), names.size());
                trans.set_next_state(csa_.get_state(std::move(state)), TransEnum::EnteringCntState);
            }
        } else {
            GuardedTransBuilder builder(csa_.ca(), cur_state_->first, byte_class);
            switch (builder.trans_type()) {
                case TransEnum::NoCondition:
                    trans.set_update(builder.no_condition(csa_));
                    break;
                case TransEnum::Small:
                    trans.set_small(builder.small(csa_));
                    break;
                case TransEnum::Lazy:
                    trans.set_lazy(new LazyTrans(std::move(builder)));
                    break;
                default:
                    FATAL_ERROR("unexpected trans type of builder", Errors::InternalFailure);
            }
        }
    }

    void add_state(State const& state) {
        if (!state.dead() && !visited.contains(state)) {
            to_visit.insert(state);
        }
    }

    void collect_next_states(Trans &trans, uint8_t symbol) {
        switch (trans.type()) {
            case TransEnum::WithoutCntState:
                add_state(trans.next_state()->first);
                break;
            case TransEnum::EnteringCntState:
                add_state(trans.next_state()->first);
                break;
            case TransEnum::NoCondition:
                add_state(trans.update()->next_state()->first);
                break;
            case TransEnum::Small:
                for (auto &update : trans.small()->updates()) {
                    add_state(update.next_state()->first);
                }
                break;
            case TransEnum::Lazy:
            {
                auto & lazy = *trans.lazy();
                auto const& guards = lazy.guards();
                std::vector<bool> eval(guards.size(), 0);
                std::string graph;
                for (size_t i = 0; i < ((size_t)1<<guards.size()); i++) {
                    for (size_t j = 0; j < guards.size(); j++) {
                        size_t d = guards.size() - j - 1; // the order of bits is descending
                        if (i & (1<<d)) {
                            eval[j] = true;
                        } else {
                            eval[j] = false;
                        }
                        auto const& update = trans.lazy()->update(i, eval, csa_);
                        add_state(update.next_state()->first);
                    }
                }
            }
                break;
            case TransEnum::NotComputed:
                assert(false);
        }
    }

    void run() {
        while (keep_going()) {
            cur_state_ = csa_.get_state(*to_visit.begin());
            to_visit.erase(to_visit.begin());
            visited.insert(cur_state_->first);
            for (uint8_t i = 0; i < csa_.ca().bytemap_range(); i++) {
                compute_full_trans(cur_state_->second[i], i);
                collect_next_states(cur_state_->second[i], i);
            }
        }
    }

    private:
    CSA& csa_;
    std::unordered_set<State> to_visit;
    std::unordered_set<State> visited;
    CachedState* cur_state_;
};

void CSA::compute_full() {
    auto state = CSATraverseState(*this);
    state.run();
}

Visualizer::Visualizer(std::string_view pattern)
    : pattern_(pattern), csa_(CA::glushkov::Builder::get_ca(pattern)) { }

std::string Visualizer::to_DOT_CSA() {
    static bool computed = false;
    if (!computed) {
        csa_.compute_full();
    }
    computed = true;
    return csa_.to_DOT();
}

std::string Visualizer::to_DOT_CA() {
    return csa_.ca().to_DOT([] (auto sym) {return std::to_string(sym);});
}

} // namespace CSA
