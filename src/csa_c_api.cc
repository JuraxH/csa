#include "csa.hh"
#include <string>
#include <iostream>

extern "C" {
    void* csa_compile(const char* pattern) {
        try {
            return new CSA::Matcher(pattern);
        } catch (...) {
            return nullptr;
        }
    }

    void csa_free(void* ptr) {
        if (ptr) {
            delete static_cast<CSA::Matcher*>(ptr);
        }
    }

    int csa_match_compiled(void* ptr, const char* text) {
        if (!ptr) return -1;
        try {
            CSA::Matcher* matcher = static_cast<CSA::Matcher*>(ptr);
            return matcher->match(text) ? 1 : 0;
        } catch (...) {
            return -1;
        }
    }

    int csa_match(const char* pattern, const char* text) {
        try {
            CSA::Matcher matcher(pattern);
            return matcher.match(text) ? 1 : 0;
        } catch (const std::exception& e) {
            std::cerr << "Regex error: " << e.what() << std::endl;
            return -1; // Indicate error
        } catch (const char* msg) {
            std::cerr << "Regex error (char*): " << msg << std::endl;
            return -1;
        } catch (const std::string& msg) {
            std::cerr << "Regex error (string): " << msg << std::endl;
            return -1;
        } catch (...) {
            std::cerr << "Unknown regex error" << std::endl;
            return -1;
        }
    }
}
