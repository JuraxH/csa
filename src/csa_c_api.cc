#include "csa.hh"
#include <string>
#include <iostream>

extern "C" {
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
