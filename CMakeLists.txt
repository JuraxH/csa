cmake_minimum_required(VERSION 3.16)

project(csa
    VERSION 1.0
    DESCRIPTION "Implementation of CSA matcher"
    LANGUAGES CXX
    )


message(STATUS "The CXX compiler: ${CMAKE_CXX_COMPILER_ID}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Wextra -Wfloat-equal -Wctor-dtor-privacy -Weffc++ -Woverloaded-virtual -fdiagnostics-show-option)
endif()

add_executable(ca_cli 
    src/ca_cli.cc
    src/ca.hh
    src/csa.cc
    src/csa_errors.hh
    src/csa.hh
    src/glushkov.cc
    src/glushkov.hh
    src/regex.hh

    re2/bitmap256.h
    re2/compile.cc
    re2/parse.cc
    re2/perl_groups.cc
    re2/pod_array.h
    re2/prog.cc
    re2/prog.h
    re2/range_builder.hh
    re2/re2.cc
    re2/re2.h
    re2/regexp.cc
    re2/regexp.h
    re2/simplify.cc
    re2/sparse_array.h
    re2/sparse_set.h
    re2/stringpiece.cc
    re2/stringpiece.h
    re2/tostring.cc
    re2/unicode_casefold.cc
    re2/unicode_casefold.h
    re2/unicode_groups.cc
    re2/unicode_groups.h
    re2/walker-inl.h

    re2/util/logging.h
    re2/util/mutex.h
    re2/util/rune.cc
    re2/util/strutil.cc
    re2/util/strutil.h
    re2/util/utf.h
    re2/util/util.h

    util/argparse.hpp
    util/ord_vector.hh
)

set_target_properties(ca_cli
  PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )

target_include_directories(ca_cli PRIVATE re2 util src re2/util)


