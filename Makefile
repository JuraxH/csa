UTIL_HEAD = logging.h mutex.h strutil.h utf.h util.h
UTIL_SRC = util/rune.cc util/strutil.cc 
UTIL_OBJ = util/rune.o util/strutil.o 

RE2_HEAD = bitmap256.h pod_array.h prog.h re2.h regexp.h sparse_array.h sparse_set.h stringpiece.h unicode_casefold.h unicode_groups.h walker-inl.h
RE2_SRC = re2/compile.cc re2/parse.cc re2/perl_groups.cc re2/prog.cc re2/re2.cc re2/regexp.cc re2/simplify.cc re2/stringpiece.cc re2/tostring.cc re2/unicode_casefold.cc re2/unicode_groups.cc
RE2_OBJ = re2/compile.o re2/parse.o re2/perl_groups.o re2/prog.o re2/re2.o re2/regexp.o re2/simplify.o re2/stringpiece.o re2/tostring.o re2/unicode_casefold.o re2/unicode_groups.o



CXXFLAGS=-std=c++14 -Wextra -Wall -Wfloat-equal -Wctor-dtor-privacy -Weffc++ -Woverloaded-virtual -fdiagnostics-show-option -g -O3 -DNDEBUG

CXX=g++

INCLUDE=-I.
#LIBS_ADD=-Lmata/build/src -Lmata/build/3rdparty/re2 -Lmata/build/3rdparty/simlib
LDFLAGS = 
LIBS=-pthread
SRC= ca.cc main.cc derivatives.cc unicode_groups.cc prog.cc unicode_casefold.cc csa.cc perl_groups.cc strutil.cc rune.cc re2parser.cc tostring.cc onepass.cc simplify.cc compile.cc regexp.cc stringpiece.cc parse.cc re2.cc 
OBJ= ca.o derivatives.o unicode_groups.o prog.o unicode_casefold.o csa.o perl_groups.o strutil.o rune.o re2parser.o tostring.o onepass.o simplify.o compile.o regexp.o stringpiece.o parse.o re2.o 
.PHONY: all clean

#all: $(patsubst %.cc,%,$(wildcard *.cc)) mata/build/src/libmata.a

re2/%.o: re2/%.cc
	$(CXX) $(INCLUDE) -c $(CXXFLAGS) $^ -o $@

util/%.o: util/%.cc
	$(CXX) $(INCLUDE) -c $(CXXFLAGS) $^ -o $@

%.o: %.cc
	$(CXX) $(INCLUDE) -c $(CXXFLAGS) $^ -o $@

main: main.o $(RE2_OBJ) $(UTIL_OBJ)
	g++ $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm *.o main re2/*.o util/*. 

