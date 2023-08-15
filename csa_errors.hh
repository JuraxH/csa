#pragma once
#include <iostream>

#define FATAL_ERROR(msg, err) do {\
    std::cerr << "Error:" << __FILE__ << ":" << __LINE__ << ": " << __func__ << ": " \
        << msg << std::endl; \
    exit(static_cast<int>(err));} while(false)


namespace CSA {

    enum class Errors {
        DoubleIncr = 10,
        InternalFailure = 11,
        NestedRepetition = 12,
        UnsupportedOperation = 13,
        InvalidUtf8 = 14,
        FailedToParse = 15,
        WeirdAnchor = 16,
    };

} // namespace CSA
