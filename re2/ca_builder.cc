#include "re2/ca_builder.hh"

namespace CA {

Fragment Builder::compute(Epsilon const &eps, CounterId cnt) {
    std::cout << "Epsilon\n"s;
    (void)cnt;
    (void)eps;
    return Fragment{{}, {}, true};
}

Fragment Builder::compute(Byte const &byte, CounterId cnt) {
    std::cout << "Byte: "s << byte.to_str() << "\n"s;
    auto s = add_state(byte.byte(), cnt);
    return Fragment{{s}, {s}, false};
}

Fragment Builder::compute(Bytes const &bytes, CounterId cnt) {
    std::cout << "Bytes: "s << bytes.to_str() << "\n"s;
    auto const& bts = bytes.bytes();
    assert(bts.size() != 0); // should not happen
    vector<UniqueByte> states(bts.size());
    for (auto i = 0; i < bts.size(); ++i) {
        states[i] = add_state(bts[i], cnt);
        if (i != 0) {
            add_transition(states[i - 1], states[i]);
        }
    }
    return Fragment{{states[0]}, {states.back()}, false};
}


Fragment Builder::compute(Alter const &alter, CounterId cnt) {
    std::cout << "Alter: "s << alter.to_str() << "\n"s;
    Fragment frag{{},{}, false};
    for (RegexpNode const &sub : alter.subs()) {
        Fragment sub_frag = visit([this, cnt] (auto const & arg) { return this->compute(arg, cnt); }, sub);
        if (sub_frag.nullable) {
            frag.nullable = true;
        }
        frag.first.reserve(frag.first.size() + sub_frag.first.size());
        std::move(sub_frag.first.begin(), sub_frag.first.end(), 
                std::inserter(frag.first, frag.first.end()));
        frag.last.reserve(frag.last.size() + sub_frag.last.size());
        std::move(sub_frag.last.begin(), sub_frag.last.end(),
                std::inserter(frag.last, frag.last.end()));
    }
    return frag;
}

Fragment Builder::compute(Concat const &concat, CounterId cnt) {
    std::cout << "Concat: "s << concat.to_str() << "\n"s;
    Fragment frag{{},{}, true};
    bool start = true;
    auto const &subs = concat.subs();
    for (auto i = 0; i < subs.size(); ++i) {
        Fragment sub_frag = visit([this, cnt] (auto const & arg) { return this->compute(arg, cnt); }, subs[i]);
        if (i != 0) {
            for (auto const& prev : frag.last) {
                for (auto const& cur: sub_frag.first) {
                    add_transition(prev, cur);
                }
            }
        }
        if (start) {
            frag.first.reserve(frag.first.size() + sub_frag.first.size());
            std::move(sub_frag.first.begin(), sub_frag.first.end(), 
                    std::inserter(frag.first, frag.first.end()));
            if (!sub_frag.nullable) {
                start = false;
                frag.nullable = false;
            }
        }
        if (frag.nullable) {
            frag.last.reserve(frag.last.size() + sub_frag.last.size());
            std::move(sub_frag.last.begin(), sub_frag.last.end(),
                    std::inserter(frag.last, frag.last.end()));
        } else {
            frag.last = std::move(sub_frag.last);
        }
    }
    return frag;
}

Fragment Builder::compute(Repeat const &repeat, CounterId cnt) {
    std::cout << "Repeat: "s << repeat.to_str() << "\n"s;
    if (repeat.min() == 0 && repeat.max() == -1) {
        Fragment frag = visit([this, cnt] (auto const & arg) { return this->compute(arg, cnt); }, repeat.sub());
        if (cnt == 0) {
            for (auto const& prev : frag.last) {
                for (auto const& cur: frag.first) {
                    add_transition_star(prev, cur);
                }
            }
        } else {
            for (auto const& prev : frag.last) {
                for (auto const& cur: frag.first) {
                    add_transition(prev, cur);
                }
            }
        }
        return frag;
    } else {
        assert(cnt == 0); // cannot have nested counters
        CounterId cnt_id = ca_.add_counter(Counter(repeat.min(), repeat.max()));
        Fragment frag = visit([this, cnt_id] (auto const & arg) { return this->compute(arg, cnt_id); }, repeat.sub());
        for (auto const& prev : frag.last) {
            for (auto const& cur: frag.first) {
                add_transition_repeat(prev, cur);
            }
        }
        if (repeat.min() == 0) {
            frag.nullable = true;
        }
        return frag;
    }
}

Fragment Builder::compute(Plus const &plus, CounterId cnt) {
    std::cout << "Plus: "s << plus.to_str() << "\n"s;
    std::cout << "Sub: "s << visit([](auto const &arg) { return arg.to_str(); }, plus.sub()) << "\n"s;
    RegexpNode epxr = RegexpNode(Concat({ plus.sub(), RegexpNode(Repeat(plus.sub(), 0, -1)) }));
    return visit([this, cnt] (auto const & arg) {
            return this->compute(arg, cnt);
            }, epxr);
}

}
