#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <memory>

#include "re2/re2.h"
#include "re2/regexp.h"
#include "re2/prog.h"
#include "csa_errors.hh"


namespace re2::regex {
    class Regex {
        public:
        Regex(std::string_view pattern) : regexp_(nullptr), options_(), prog_(),
        bytemap_(nullptr), bytemap_range_(0) {
            options_.set_dot_nl();
            regexp_ = re2::Regexp::Parse(pattern, 
                    static_cast<re2::Regexp::ParseFlags>(options_.ParseFlags()), 
                    nullptr);
            if (regexp_ == nullptr) {
                FATAL_ERROR("Parsing of regex failed", CSA::Errors::FailedToParse);
            }

            prog_ = regexp_->CompileToProg(options_.max_mem() * 2 / 3);
            if (prog_ == nullptr) {
                FATAL_ERROR("Building of bytemap failed", CSA::Errors::FailedToParse);
            }

            bytemap_range_ = prog_->bytemap_range();
            bytemap_ = prog_->bytemap();
        }
        
        Regex(Regex const&) = delete;
        Regex& operator=(Regex const&) = delete;
        Regex(Regex&&) = delete;
        Regex& operator=(Regex&&) = delete;

        re2::Regexp* regexp() {
            return regexp_;
        }

        uint8_t const* bytemap() const {
            return bytemap_;
        }

        int bytemap_range() const {
            return bytemap_range_;
        }

        ~Regex() { 
            if (regexp_ != nullptr) {
                regexp_->Decref();
            }
            if (prog_ != nullptr) {
                delete prog_;
            }
        }

        private:
        re2::Regexp* regexp_; // owining ref
        RE2::Options options_;
        re2::Prog* prog_;
        uint8_t *bytemap_;  // owned by prog
        int bytemap_range_;
    };
}
