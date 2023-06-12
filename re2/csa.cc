#include "re2/csa.hh"
#include "re2/glushkov.hh"

namespace CSA {

using namespace std;

CSA::CSA(string const& pattern) : ca(CA::glushkov::Builder::get_ca(pattern)), 
    configs({make_pair(InitConfig, vector<Trans>(ca.bytemap_range())), }), counter_buffer() { }

bool CSA::match(const std::string& text) {
    CachedConfig *cur = &(*configs.find(InitConfig));
    int byte_class;
    counter_buffer.resize(0);

    for (unsigned char c : text) {
        byte_class = ca.get_byte_class(c);
        cur = get_next_config(cur, byte_class);

        if (cur->first.normal_states.empty() 
                && cur->first.counter_states.empty()
           ) {
            return false;
        }

    }

    return test_final_condition(cur);
}

bool CSA::test_final_condition(CachedConfig *cur) {
    for (CA::StateId state : cur->first.normal_states) {
        if (ca.get_state(state).final() == CA::Guard::True) {
            return true;
        }
    }
    for (CounterState const& state : cur->first.counter_states) {
        auto const& ca_state = ca.get_state(state.state());
        switch (ca_state.final()) {
            case CA::Guard::True:
                return true;
            case CA::Guard::False:
                break;
            case CA::Guard::CanIncr:
                exit(static_cast<int>(Errors::INTERNAL_FAIL));
            case CA::Guard::CanExit: {
                auto const& cnt = ca.get_counter(ca_state.cnt());
                for (auto index : state.normal()) {
                    if (cnt.can_exit(counter_buffer[index].offset() 
                                - counter_buffer[index].list().back())) {
                        return true;
                    }
                }
                for (auto index : state.plus()) {
                    // todo: maybe there should be control for case when incremeted counter is out of bounds and lower one must be checked
                    if (cnt.can_exit(1 + counter_buffer[index].offset() 
                                - counter_buffer[index].list().back())) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

CachedConfig *CSA::get_next_config(CachedConfig *cur, int byte_class) {
    Trans& trans = cur->second[byte_class];
    if (trans.type == TransEnum::NOT_COMPUTED) {
        compute_trans(trans, cur, byte_class);
    }

    switch (trans.type) {
        case TransEnum::WITHOUT_COUNTER:
            return trans.new_config;
        case TransEnum::SIMPLE:
            execute_update(*(trans.update), 
                    trans.update->new_config->first.counter_buffer_size);
            return trans.update->new_config;
        case TransEnum::SMALL: {
            auto index = get_guard_index(cur->first, trans.small_trans->guards);
            execute_update(trans.small_trans->updates[index],
                    trans.small_trans->updates[index].new_config->first.counter_buffer_size);
            return trans.small_trans->updates[index].new_config;
            }
        case TransEnum::LARGE: {
            auto *update = &get_update_large(cur, *trans.partialy_computed_trans);
            execute_update(*update, update->new_config->first.counter_buffer_size);
            return update->new_config;
            }
        case TransEnum::NOT_COMPUTED:
        default:
            exit(static_cast<int>(Errors::INTERNAL_FAIL));
    }
}

void CSA::execute_update(Update &update, unsigned buffer_size) {
    switch(update.type) {
        case UpdateEnum::ID:
            break;
        case UpdateEnum::ENTER:
            counter_buffer.resize(buffer_size, CounterSet(1));
            break;
        case UpdateEnum::KEEP_BUFFER:
            for (auto &inst : update.prog) {
                switch (inst.instruction) {
                    case CntSetInstEnum::INC:
                        counter_buffer[inst.index].increment(inst.max);
                        break;
                    case CntSetInstEnum::INSERT1:
                        counter_buffer[inst.target].insert_1();
                        break;
                    case CntSetInstEnum::RST_TO_1:
                        counter_buffer[inst.origin] = move(CounterSet(1));
                        break;
                    case CntSetInstEnum::MOVE:
                        exit(static_cast<int>(Errors::INTERNAL_FAIL));
                }
            }
            
            break;
        case UpdateEnum::NEW_BUFFER:
            CounterBuffer new_buffer(buffer_size, CounterSet());
            for (auto &inst : update.prog) {
                switch (inst.instruction) {
                    case CntSetInstEnum::INC:
                        counter_buffer[inst.index].increment(inst.max);
                        break;
                    case CntSetInstEnum::INSERT1:
                        new_buffer[inst.target].insert_1();
                        break;
                    case CntSetInstEnum::MOVE:
                        new_buffer[inst.target].merge(move(counter_buffer[inst.origin]));
                        break;
                    case CntSetInstEnum::RST_TO_1:
                        exit(static_cast<int>(Errors::INTERNAL_FAIL));
                }
            }
            counter_buffer = new_buffer;
            break;
    }
}

void CSA::compute_trans(Trans &trans, CachedConfig *cur, int byte_class) {
    NormalStateVec new_normal_states;
    CounterStateVec new_counter_states;
    CountersToReset rst;
    for (CA::StateId state_id : cur->first.normal_states) {
        CA::State const& state = ca.get_state(state_id);
        for (auto &ca_trans : state.transitions()) {
            if (ca_trans.symbol() == byte_class || ca_trans.symbol() == ca.bytemap_range()) {
                auto const& target_cnt = ca.get_state(ca_trans.target()).cnt();
                if (target_cnt != CA::NoCounter) {
                    new_counter_states.insert(CounterState{ca_trans.target(), {}, {}});
                    rst.add_state(ca_trans.target(), target_cnt);
                } else {
                    new_normal_states.insert(ca_trans.target());
                }
            }
        }
    }
    if (cur->first.counter_states.size() == 0) {
        compute_trans_no_counter(trans, cur, move(rst), move(new_normal_states),
                move(new_counter_states));
    } else {
        LValueTable lval_tab(cur->first.counter_buffer_size);
        std::vector<std::vector<std::pair<LValue, unsigned>>> guarded_lvals;
        std::vector<std::vector<CA::StateId>> guarded_reset;
        std::vector<Guard> guards;
        std::vector<std::vector<CA::StateId>> guarded_states;
        int index = 0;
        for (auto state : cur->first.counter_states) {
            int can_incr_index = -1;
            int can_exit_index = -1;
            for (auto const& ca_trans : ca.get_state(state.state()).transitions()) {
                if (ca_trans.symbol() == ca.bytemap_range()
                        || ca_trans.symbol() == byte_class) {
                    switch(ca_trans.grd()) {
                        case CA::Guard::CanIncr:
                            if (can_incr_index == -1) {
                                guards.push_back(Guard{state.state(), CA::Guard::CanIncr});
                                can_incr_index = index;
                                index++;
                                guarded_lvals.push_back({});
                                guarded_reset.push_back({});
                                guarded_states.push_back({});
                            } 
                            for (unsigned i : state.normal()) {
                                guarded_lvals[can_incr_index].push_back(
                                        make_pair(
                                            LValue{ca_trans.target(), LValueType::PLUS},
                                            i)
                                        );
                            }
                            if (!state.plus().empty()) {
                                exit(static_cast<int>(Errors::DOUBLE_INCREMENT));
                            }
                            break;
                        case CA::Guard::CanExit:
                            if (can_exit_index == -1) {
                                guards.push_back({state.state(), CA::Guard::CanExit});
                                can_exit_index = index;
                                index++;
                                guarded_lvals.push_back({});
                                guarded_reset.push_back({});
                                guarded_states.push_back({});
                            } 
                            switch (ca_trans.op()) {
                                case CA::Operator::ID:
                                    guarded_states[can_exit_index].push_back(ca_trans.target());
                                    break;
                                case CA::Operator::Rst:
                                    guarded_reset[can_exit_index].push_back(ca_trans.target());
                                    break;
                                default:
                                    exit(static_cast<int>(Errors::INTERNAL_FAIL));
                                    break;
                            }
                            break;
                        case CA::Guard::True:
                            new_counter_states.insert({ca_trans.target(), {}, {}});
                            switch (ca_trans.op()) {
                                case CA::Operator::ID:
                                    for (unsigned i : state.normal()) {
                                        lval_tab.add_lval(LValue{ca_trans.target(), LValueType::NOOP}, i);
                                    }
                                    for (unsigned i : state.plus()) {
                                        lval_tab.add_lval(LValue{ca_trans.target(), LValueType::PLUS}, i);
                                    }
                                    break;
                                case CA::Operator::Rst:
                                    rst.add_state(ca_trans.target(), ca.get_state(ca_trans.target()).cnt());
                                    break;
                                default:
                                    exit(static_cast<int>(Errors::INTERNAL_FAIL));
                                    break;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        if (index == 0) {
            build_simple_trans(trans, cur, new_normal_states, new_counter_states, lval_tab, rst);
        } else if (index <= small_trans_condition_limit) {
            build_small_trans(trans, cur, new_normal_states,
                    new_counter_states, lval_tab, rst, guarded_lvals,
                    guarded_reset, guarded_states, move(guards)
                    );
        } else {
            trans.type = TransEnum::LARGE;
            trans.partialy_computed_trans = new PartialyComputedTrans(
                    move(new_normal_states),
                    move(new_counter_states),
                    move(lval_tab),
                    move(rst),
                    move(guards),
                    move(guarded_states),
                    move(guarded_lvals),
                    move(guarded_reset)
                    );
        }
    }
}


bool CSA::eval_guard(Guard &guard, const Config &config) {
    CounterState state = *config.counter_states.find(CounterState{guard.state, {}, {}});
    auto const& cnt = ca.get_counter(ca.get_state(state.state()).cnt());
    if (guard.condition == CA::Guard::CanIncr) {
        for (unsigned counter_set : state.normal()) {
            if (cnt.can_incr(counter_buffer[counter_set].offset()
                - *counter_buffer[counter_set].list().begin())
            ) {
                return true;
            }
        }
        for (unsigned counter_set : state.plus()) {
            if (cnt.can_incr(counter_buffer[counter_set].offset() + 1
                - *counter_buffer[counter_set].list().begin())
            ) {
                return true;
            }
        }

    } else {
        for (unsigned counter_set : state.normal()) {
            if (cnt.can_exit(counter_buffer[counter_set].offset()
                - counter_buffer[counter_set].list().back())
            ) {
                return true;
            }
        }
        for (unsigned counter_set : state.plus()) {
            if (cnt.can_exit(counter_buffer[counter_set].offset() + 1
                - counter_buffer[counter_set].list().back())
            ) {
                return true;
            }
        }
    }
    return false;
}

Update& CSA::get_update_large(CachedConfig *cur,
        PartialyComputedTrans &part_trans) {
    unsigned res = 0;
    vector<Guard>::iterator it = part_trans.guards.begin();
    vector<bool> sat_guards(part_trans.guards.size(), false);
    for (unsigned i = 0; i < part_trans.guards.size(); ++i, ++it) {
        if (eval_guard(*it, cur->first)) {
            res += 1<<i;
            sat_guards[i] = true;
        }
    }
    auto update = part_trans.updates.find(res);
    if (update == part_trans.updates.end()) {
        part_trans.updates[res] = get_update_for_evaluation(sat_guards, cur, 
                part_trans.normal_states,
                part_trans.counter_states,
                part_trans.lval_tab,
                part_trans.reset,
                part_trans.guarded_lvals,
                part_trans.guarded_reset,
                part_trans.guarded_states
                );
    }
    return part_trans.updates[res];
}
 
void CSA::compute_trans_no_counter(
        Trans &trans,
        CachedConfig *cur,
        CountersToReset&& rst,
        NormalStateVec&& new_normal_states,
        CounterStateVec&& new_counter_states
        ) {
    Config new_config{std::move(new_normal_states), std::move(new_counter_states), 0};
    if (new_config.counter_states.size() == 0) {
        trans.type = TransEnum::WITHOUT_COUNTER;
        ConfigCache::iterator it = configs.find(new_config);
        if (it == configs.end()) {
            it = configs.insert(it,
                    std::make_pair(
                        std::move(new_config),
                        std::vector<Trans>(ca.bytemap_range())
                        )
                    );
        }
        trans.new_config = &(*it);
    } else {
        trans.type = TransEnum::SIMPLE;
        OrdVector<OrdVector<CA::StateId>> indexes;
        for (auto &rst_counter : rst.counters) {
            indexes.insert(move(rst_counter.states));
        }
        OrdVector<OrdVector<CA::StateId>>::iterator it = indexes.begin();
        for (unsigned i = 0; i < indexes.size(); ++i, ++it) {
            for (CA::StateId state : *it) {
                auto counter_state = new_config.counter_states.find({state, {}, {}});
                counter_state->normal().insert(i);
            }
        }
        new_config.counter_buffer_size = indexes.size();
        ConfigCache::iterator config = configs.find(new_config);
        if (config == configs.end()) {
            config = configs.insert(config,
                    std::make_pair(
                        std::move(new_config),
                        std::vector<Trans>(ca.bytemap_range())
                        )
                    );
        }
        trans.update = new Update{UpdateEnum::ENTER, &(*config), {}};
    }
}

UpdateEnum CSA::get_update_type(std::vector<CntSetInst> &inst_vec,
        unsigned old_buffer_size,
        unsigned new_buffer_size
        ) {
    if (old_buffer_size != new_buffer_size) {
        return UpdateEnum::NEW_BUFFER;
    }
    bool all_moves_usseles = true;
    vector<bool> moved_to(new_buffer_size, false);
    for (auto &inst : inst_vec) {
        if (inst.instruction == CntSetInstEnum::MOVE) {
            if (inst.origin != inst.target) {
                all_moves_usseles = false;
                break;
            }
            moved_to[inst.target] = true;
        }
    }
    if (!all_moves_usseles) {
        return UpdateEnum::NEW_BUFFER;
    }
    vector<CntSetInst> new_inst_vec;
    for (auto &inst : inst_vec) {
        switch (inst.instruction) {
            case CntSetInstEnum::INC:
                new_inst_vec.push_back(inst);
                break;
            case CntSetInstEnum::INSERT1:
                if (moved_to[inst.target]) {
                    new_inst_vec.push_back(inst);
                } else {
                    inst.instruction = CntSetInstEnum::RST_TO_1;
                    new_inst_vec.push_back(inst);
                }
                break;
            default:
                break;
        }
    }
    inst_vec = move(new_inst_vec);
    return UpdateEnum::KEEP_BUFFER;
}

UpdateEnum CSA::build_update(
        CounterStateVec &new_counter_states,
        LValueTable &lval_tab,
        CountersToReset &rst,
        unsigned &buffer_size,
        std::vector<CntSetInst> &inst_vec
        ) {
    OrdVector<OrdVector<CA::StateId>> state_indexes{};
    vector<vector<LValue>>::iterator row = lval_tab.tab.begin();
    for (unsigned i = 0; i < lval_tab.tab.size(); ++i, ++row) {
        bool all_plus = true;
        OrdVector<CA::StateId> states;
        for (auto lval : *row) {
            if (lval.type != LValueType::PLUS) {
                all_plus = false;
            }
            states.insert(lval.state);
        }
        if (row->size() != 0) {
            if (all_plus) {
                CntSetInst op{};
                op.instruction = CntSetInstEnum::INC;
                op.index = i;
                op.max = ca.get_counter(ca.get_state((*row)[0].state).cnt()).max();
                inst_vec.push_back(op);
                for (auto &lval : *row) {
                    lval.type = LValueType::NOOP;
                }
            }
            state_indexes.insert(move(states));
        }
    }
    for (auto &rst_cnt : rst.counters) {
        state_indexes.insert(rst_cnt.states);
    }
    buffer_size = state_indexes.size();
    row = lval_tab.tab.begin();
    for (unsigned i = 0; i < lval_tab.tab.size(); ++i, ++row) {
        if (row->size()) {
            OrdVector<CA::StateId> states;
            for (auto lval : *row) {
                states.insert(lval.state);
            }
            unsigned index = state_indexes.get_index(states);
            CntSetInst op{};
            op.instruction = CntSetInstEnum::MOVE;
            op.origin = i;
            op.target = index;
            inst_vec.push_back(op);
            for (auto lval : *row) {
                if (lval.type == LValueType::NOOP) {
                    new_counter_states.find({lval.state, {}, {}})->normal().insert(index);
                } else {
                    new_counter_states.find({lval.state, {}, {}})->plus().insert(index);
                }
            }
        }
    }
    for (auto &cnt_rst : rst.counters) {
        unsigned index = state_indexes.get_index(cnt_rst.states);
        CntSetInst op{};
        op.instruction = CntSetInstEnum::INSERT1;
        op.origin = index;
        op.target = index;
        inst_vec.push_back(op);
        for (auto &state : cnt_rst.states) {
            new_counter_states.find({state, {}, {}})->normal().insert(index);
        }
    }
    return get_update_type(inst_vec, lval_tab.tab.size(), buffer_size);
}

unsigned CSA::get_guard_index(const Config &config, std::vector<Guard> &guards) {
    unsigned res = 0;
    vector<Guard>::iterator it = guards.begin();
    for (unsigned i = 0; i < guards.size(); ++i, ++it) {
        if (eval_guard(*it, config)) {
            res += 1<<i;
        }
    }
    return res;
}

void CSA::build_simple_trans(Trans &trans,
        CachedConfig *cur_config,
        NormalStateVec &new_normal_states,
        CounterStateVec &new_counter_states,
        LValueTable &lval_tab,
        CountersToReset &rst
        ) {
    unsigned buffer_size;
    std::vector<CntSetInst> inst_vec;
    trans.type = TransEnum::SIMPLE;
    UpdateEnum type = build_update(new_counter_states, lval_tab, rst,
            buffer_size, inst_vec
            );
    trans.update = new Update{type, nullptr, move(inst_vec)};
    Config new_config{move(new_normal_states), move(new_counter_states), buffer_size};
    ConfigCache::iterator it = configs.find(new_config);
    if (it == configs.end()) {
        it = configs.insert(it,
                std::make_pair(
                    std::move(new_config),
                    std::vector<Trans>(ca.bytemap_range())
                    )
                );
    }
    trans.update->new_config = &(*it);
}

Update CSA::get_update_for_evaluation(
        std::vector<bool> &sat_guards,
        CachedConfig *cur_config,
        NormalStateVec normal_states,
        CounterStateVec counter_states,
        LValueTable lval_tab,
        CountersToReset rst,
        std::vector<std::vector<std::pair<LValue, unsigned>>> &guarded_lvals,
        std::vector<std::vector<CA::StateId>> &guarded_reset,
        std::vector<std::vector<CA::StateId>> &guarded_states
        ) {
    vector<bool>::iterator it = sat_guards.begin();
    for (unsigned i = 0; i < sat_guards.size(); ++i, ++it) {
        if (*it) {
            for (auto &lval : guarded_lvals[i]) {
                lval_tab.add_lval(lval.first, lval.second);
                counter_states.insert({lval.first.state, {}, {}});
            }
            for (CA::StateId state : guarded_reset[i]) {
                counter_states.insert({state, {}, {}});
                rst.add_state(state, ca.get_state(state).cnt());
            }
            for (CA::StateId state : guarded_states[i]) {
                normal_states.insert(state);
            }
        }
    }
    unsigned buffer_size;
    vector<CntSetInst> inst_vec{};
    UpdateEnum type = build_update(counter_states, lval_tab, rst, buffer_size, inst_vec);
    Config new_config{move(normal_states), move(counter_states), buffer_size};
    ConfigCache::iterator config = configs.find(new_config);
    if (config == configs.end()) {
        config = configs.insert(config,
                std::make_pair(
                    std::move(new_config),
                    std::vector<Trans>(ca.bytemap_range())
                    )
                );
    }
    return Update{type, &(*config), move(inst_vec)};
}

// for now will only work for 2 or 1
void CSA::build_small_trans(Trans &trans,
        CachedConfig *cur_config,
        NormalStateVec &normal_states,
        CounterStateVec &counter_states,
        LValueTable &lval_tab,
        CountersToReset &rst,
        std::vector<std::vector<std::pair<LValue, unsigned>>> &guarded_lvals,
        std::vector<std::vector<CA::StateId>> &guarded_reset,
        std::vector<std::vector<CA::StateId>> &guarded_states,
        std::vector<Guard> &&guards
        ) {
    assert(guards.size() <=2);
    trans.type = TransEnum::SMALL;
    trans.small_trans = new SmallTrans{};
    trans.small_trans->guards = move(guards);
    // realy stupid way to do it but i am not in a mood to think
    vector<bool> sat_guards;
    if (trans.small_trans->guards.size() == 1) {
        trans.small_trans->updates.resize(2);
        sat_guards = vector<bool>{false,};
        trans.small_trans->updates[0] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
        sat_guards = vector<bool>{true,};
        trans.small_trans->updates[1] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
    } else {
        trans.small_trans->updates.resize(4);
        sat_guards = vector<bool>{false, false};
        trans.small_trans->updates[0] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
        sat_guards = vector<bool>{true, false,};
        trans.small_trans->updates[1] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
        sat_guards = vector<bool>{false, true,};
        trans.small_trans->updates[2] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
        sat_guards = vector<bool>{true, true, };
        trans.small_trans->updates[3] = get_update_for_evaluation(sat_guards, 
        cur_config, normal_states, counter_states, lval_tab, rst, guarded_lvals,
        guarded_reset, guarded_states
        );
    }
}
    
}
