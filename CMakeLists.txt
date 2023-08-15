cmake_minimum_required(VERSION 3.16)

project(csa
    VERSION 1.0
    DESCRIPTION "Implementation of CSA matcher"
    LANGUAGES CXX
    )

add_compile_options(-Wall -Wextra -Wfloat-equal -Wctor-dtor-privacy -Weffc++ -Woverloaded-virtual -fdiagnostics-show-option -g -O3)

add_executable(csa_bench 
    csa_bench.cc
    util/logging.h
    util/mutex.h
    util/strutil.h
    util/utf.h
    util/util.h
    util/ord_vector.hh
    util/rune.cc
    util/strutil.cc 
    re2/bitmap256.h
    re2/pod_array.h
    re2/prog.h
    re2/re2.h
    re2/regexp.h
    re2/sparse_array.h
    re2/sparse_set.h
    re2/stringpiece.h
    re2/unicode_casefold.h
    re2/unicode_groups.h
    re2/walker-inl.h
    re2/glushkov.cc
    re2/compile.cc
    re2/parse.cc
    re2/perl_groups.cc
    re2/prog.cc
    re2/re2.cc
    re2/regexp.cc
    re2/simplify.cc
    re2/stringpiece.cc
    re2/tostring.cc
    re2/unicode_casefold.cc
    re2/unicode_groups.cc
    csa.hh
    csa.cc
    )

target_include_directories(csa_bench PUBLIC . re2 util)

target_compile_features(csa_bench PUBLIC cxx_std_20)


