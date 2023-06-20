#include "csa.hh"
#include "re2/ca.hh"
#include "re2/glushkov.hh"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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
    if (list_.front() != offset_ - 1) {
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
    for (auto& name: cnts_to_reset_.get_cnt_set_names()) {
        state_indexes.insert(std::move(name));
    }
    return state_indexes;
}

Update TransBuilder::compute_update(CSA& csa) {
    Update update;
    auto state_indexes = compute_state_idexes(csa, update);
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
    update.optimize(state_indexes.size());
    State state(std::move(normal_), std::move(counter_), state_indexes.size());
    update.set_next_state(csa.get_state(std::move(state)));
    return update;
}

GuardedTransBuilder::GuardedTransBuilder(CA::CA<uint8_t>& ca,
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

void GuardedTransBuilder::add_cnt_state(CA::CA<uint8_t> &ca,
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
const std::vector<bool> small_size_2_2{false, true};
const std::vector<bool> small_size_2_3{true, false};
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
    if (guard == CA::Guard::CanIncr) {
        auto max = csa_.ca().get_counter(csa_.ca().get_state(cnt_state.state()).cnt()).max();
        for (auto i : cnt_state.actual()) {
            if (cnt_sets_[i].min() < static_cast<unsigned>(max)) {
                return true;
            }
        }
        if (!cnt_state.postponed().empty()) {
            FATAL_ERROR("testing guard on state that has postponed incr", Errors::DoubleIncr);
        }
        return false;
    } else { // CanExit
        auto min = csa_.ca().get_counter(csa_.ca().get_state(cnt_state.state()).cnt()).min();
        for (auto i : cnt_state.actual()) {
            if (cnt_sets_[i].max() >= static_cast<unsigned>(min)) {
                return true;
            }
        }
        if (!cnt_state.postponed().empty()) {
            FATAL_ERROR("testing guard on state that has postponed incr", Errors::DoubleIncr);
        }
        return false;
    }
}

void Config::execute_update(Update const& update) {
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
                        cnt_sets_tmp_[inst.index()].increment(inst.max());
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

std::string CountingSet::to_str() const {
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

} // namespace CSA
