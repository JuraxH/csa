#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/prog.h"
#include "re2/ca.hh"


namespace CA
{
    using std::string;
    const size_t bytemap_size = 256;

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
                if (!is_nullable(regexp->sub()[i])) {
                    return true;
                }
            }
        }
        return false;
    }

    // consumes the given regexp
    re2::Regexp *normalize(re2::Regexp *regexp) {
        if (regexp->op() == re2::kRegexpQuest) {
            re2::Regexp *subs[2];
            regexp->sub()[0] = normalize(regexp->sub()[0]);
            subs[0] = regexp->sub()[0]->Incref();
            subs[1] = re2::Regexp::EmptyMatch(regexp->parse_flags());
            auto tmp = re2::Regexp::AlternateNoFactor(subs, 2, regexp->parse_flags());
            regexp->Decref();
            return tmp;
        }
        if (regexp->op() == re2::kRegexpPlus) {
            re2::Regexp *subs[2];
            regexp->sub()[0] = normalize(regexp->sub()[0]);
            subs[0] = regexp->sub()[0]->Incref();
            subs[1] = re2::Regexp::Star(regexp->sub()[0]->Incref(), regexp->parse_flags());
            auto tmp = re2::Regexp::Concat(subs, 2, regexp->parse_flags());
            regexp->Decref();
            return tmp;
        }
        for (auto i = 0; i < regexp->nsub(); i++) {
            regexp->sub()[i] = normalize(regexp->sub()[i]);
        }
        return regexp;
    }

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
