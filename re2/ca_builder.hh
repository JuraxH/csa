#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/prog.h"
#include "re2/ca.hh"


namespace CA
{
    using std::string;
    const size_t bytemap_size = 256;


    re2::Regexp *remove_first_sub(re2::Regexp *regexp) {
        if (regexp->nsub() <= 1) {
            return re2::Regexp::EmptyMatch(regexp->parse_flags());
        }
        re2::Regexp *subs[regexp->nsub() - 1];
        for (auto i = 1; i < regexp->nsub(); i++) {
            subs[i-1] = regexp->sub()[i];
        }
        return re2::Regexp::Concat(subs, regexp->nsub() - 1, regexp->parse_flags()); 
    }

    re2::Regexp *combine_for_concat(re2::Regexp *r1, re2::Regexp *r2) {
        if (r1->op() == re2::kRegexpEmptyMatch) {
            return remove_first_sub(r2);
        }
        if (r2->nsub() <= 1) {
            return r1->Incref();
        }
        re2::Regexp *subs[r2->nsub()];
        subs[0] = r1->Incref();
        for (auto i = 1; i < r2->nsub(); i++) {
            subs[i] = r2->sub()[i];
        }
        return re2::Regexp::Concat(subs, r2->nsub(), r2->parse_flags()); 
    }

    // TODO: remove recursion
    // consumes the given regexp
    re2::Regexp *remove_captures(re2::Regexp *regexp) {
        if (regexp->op() == re2::kRegexpCapture) {
            auto tmp = regexp->sub()[0]->Incref();
            regexp->Decref();
            return remove_captures(tmp);
        }
        for (auto i = 0; i < regexp->nsub(); i++) {
            regexp->sub()[i] = remove_captures(regexp->sub()[i]);
        }
        return regexp;
    }

    // the regexp must not contain any kRegexpCapture nodes
    bool is_nullable(re2::Regexp *regexp) {
        auto is_nullable_loop = regexp->op() == re2::kRegexpRepeat 
            && (regexp->min() == 0 || is_nullable(regexp->sub()[0]));
        if (is_nullable_loop 
                || regexp->op() == re2::kRegexpStar 
                || regexp->op() == re2::kRegexpQuest
                || regexp->op() == re2::kRegexpEmptyMatch) {
            return true;
        }
        if (regexp->op() == re2::kRegexpConcat) {
            for (auto i = 0; i < regexp->nsub(); i++) {
                if (!is_nullable(regexp->sub()[i])) {
                    return false;
                }
            }
            return true;
        }
        if (regexp->op() == re2::kRegexpAlternate) {
            for (auto i = 0; i < regexp->nsub(); i++) {
                if (is_nullable(regexp->sub()[i])) {
                    return true;
                }
            }
        }
        return false;
    }

    // consumes the given regexp
    re2::Regexp *normalize(re2::Regexp *regexp) {
        // a? -> (:?)|a
        if (regexp->op() == re2::kRegexpQuest) {
            re2::Regexp *subs[2];
            regexp->sub()[0] = normalize(regexp->sub()[0]);
            subs[0] = regexp->sub()[0]->Incref();
            subs[1] = re2::Regexp::EmptyMatch(regexp->parse_flags());
            auto tmp = re2::Regexp::AlternateNoFactor(subs, 2, regexp->parse_flags());
            regexp->Decref();
            return tmp;
        }
        // a+ -> aa*
        if (regexp->op() == re2::kRegexpPlus) {
            re2::Regexp *subs[2];
            regexp->sub()[0] = normalize(regexp->sub()[0]);
            subs[0] = regexp->sub()[0]->Incref();
            subs[1] = re2::Regexp::Star(regexp->sub()[0]->Incref(), regexp->parse_flags());
            auto tmp = re2::Regexp::Concat(subs, 2, regexp->parse_flags());
            regexp->Decref();
            return tmp;
        }
        // (:?a*){3,3} -> (:?a*){0,3}
        if (regexp->op() == re2::kRegexpRepeat) {
            regexp->sub()[0] = normalize(regexp->sub()[0]);
            if (regexp->min() == 0 || is_nullable(regexp->sub()[0])) {
                regexp->null_loop();
            }
            return regexp;
        }
        for (auto i = 0; i < regexp->nsub(); i++) {
            regexp->sub()[i] = normalize(regexp->sub()[i]);
        }
        return regexp;
    }

    enum class EquationType {
        Epsilon,
        Concat,
        Alternate,
        Star,
        Repeat,
    };

    class Equation {
        public:
        // TODO: add support for unicode
        static Equation get_eq(re2::Regexp *regexp) {
            switch (regexp->op()) {
                case re2::kRegexpNoMatch:
                case re2::kRegexpEmptyMatch:
                    return Equation(EquationType::Epsilon, nullptr, nullptr); 
                case re2::kRegexpLiteral:
                    return Equation(EquationType::Concat, regexp->Incref(),
                            re2::Regexp::EmptyMatch(regexp->parse_flags()));
                case re2::kRegexpLiteralString: {
                    // TODO: use runes instead of chars
                    auto op1 = re2::Regexp::NewLiteral(regexp->runes()[0], regexp->parse_flags());
                    if (regexp->nrunes() == 1) {
                        return Equation(EquationType::Concat, op1,
                                re2::Regexp::EmptyMatch(regexp->parse_flags()));
                    }
                    auto op2 = re2::Regexp::LiteralString(&regexp->runes()[1],
                            regexp->nrunes() - 1, regexp->parse_flags());
                    return Equation(EquationType::Concat, op1, op2);
                }
                case re2::kRegexpConcat:
                    return get_concat_eq(regexp);
                case re2::kRegexpAlternate:
                    return Equation(EquationType::Alternate, regexp->Incref(),
                            re2::Regexp::EmptyMatch(regexp->parse_flags()));
                case re2::kRegexpStar:
                    return Equation(EquationType::Star, regexp->Incref(),
                            re2::Regexp::EmptyMatch(regexp->parse_flags()));
                case re2::kRegexpRepeat:
                    return Equation(EquationType::Repeat, regexp->Incref(),
                            re2::Regexp::EmptyMatch(regexp->parse_flags()));
                case re2::kRegexpAnyChar:
                case re2::kRegexpAnyByte:
                case re2::kRegexpBeginLine:
                case re2::kRegexpEndLine:
                case re2::kRegexpWordBoundary:
                case re2::kRegexpNoWordBoundary:
                case re2::kRegexpBeginText:
                case re2::kRegexpEndText:
                case re2::kRegexpCharClass:
                    return Equation(EquationType::Concat, regexp->Incref(),
                            re2::Regexp::EmptyMatch(regexp->parse_flags()));
                default:
                    throw std::runtime_error("Use of unimplemented RegexpOp: "s
                            + std::to_string(regexp->op()) + " in get_eq()");
            }
        }

        static Equation get_concat_eq(re2::Regexp *regexp) {
            auto subeq = get_eq(regexp->sub()[0]);
            switch (subeq.type_) {
                using enum EquationType;
                case Epsilon:
                    return get_eq(remove_first_sub(regexp));
                case Concat:
                    return Equation(EquationType::Concat, subeq.op1_->Incref(),
                            combine_for_concat(subeq.op2_, regexp));
                case Alternate:
                    return Equation(EquationType::Alternate, subeq.op1_->Incref(),
                            combine_for_concat(subeq.op2_, regexp));
                case Star:
                    return Equation(EquationType::Star, subeq.op1_->Incref(),
                            combine_for_concat(subeq.op2_, regexp));
                case Repeat:
                    return Equation(EquationType::Repeat, subeq.op1_->Incref(),
                            combine_for_concat(subeq.op2_, regexp));
            }
        }

        Equation &operator=(Equation &&other) {
            type_ = other.type_;
            op1_ = other.op1_;
            op2_ = other.op2_;
            other.op1_ = nullptr;
            other.op2_ = nullptr;
            return *this;
        }

        ~Equation() { 
            if (op1_ != nullptr) op1_->Decref(); 
            if (op2_ != nullptr) op2_->Decref(); 
        }

        private:
        // the oprands are consumed
        Equation(EquationType type, re2::Regexp *op1, re2::Regexp *op2)
            : type_(type), op1_(op1), op2_(op2) {}
        EquationType type_;
        re2::Regexp *op1_;
        re2::Regexp *op2_;
    };

    class Builder {
        public:
        Builder(string const &pattern) : regexp_(nullptr), options_(), 
                prog_(nullptr), bytemap_(nullptr), bytemap_range_() {
            regexp_ = re2::Regexp::Parse(pattern, 
                    static_cast<re2::Regexp::ParseFlags>(options_.ParseFlags()), 
                    nullptr);
            if (regexp_ == nullptr) {
                throw std::runtime_error("Parsing of regex failed");
            }
            prog_ = regexp_->CompileToProg(options_.max_mem() * 2 / 3);
            if (prog_ == nullptr) {
                throw std::runtime_error("Building of bytemap failed");
            }
            bytemap_range_ = prog_->bytemap_range();
            bytemap_ = prog_->bytemap();
        }


        re2::Regexp *regexp() { return regexp_; }
        re2::Prog *prog() { return prog_; }
        int bytemap_range() { return bytemap_range_; }

        ~Builder() {
            if (regexp_ != nullptr) {
                regexp_->Decref();
            }
            if (prog_ != nullptr) {
                delete prog_;
            }
        }

        private:
            re2::Regexp *regexp_;
            RE2::Options options_;
            re2::Prog *prog_;
            uint8_t *bytemap_;
            int bytemap_range_;
    };
}
