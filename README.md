# Counting Set Automata
This repository contains implementation of regex matching
algorithm described in [[1]](#1). For parsing of the regexes RE2 [[2]](#2)
parser is used.

Main purpose of this implementation was benchmarking so only basic
usage is supported.

## Building
Two builds are possible, directly using `cmake` or using `nix build`.
The advantage of the nix build is that it does not require having c++20
supporting compiler.

### Using cmake
The program `cmake` and compiler supporting c++20 is required.

To run the build execute the 2 following commands:
```
cmake -B build -S .
cmake --build build
```
Now there should be executable `ca_cli` in the build directory.

### Using nix
To perform this build nix must be installed and experimental features
[nix command and flakes](https://nixos.wiki/wiki/Flakes).

To build just run the `nix build` command.
```
$ nix build
$ ls result/bin
ca_cli
```

## Usage
The program `ca_cli` has 2 sub commands `debug` which can be used to
get the DOT graph representation of used automata and `lines` used
for benchmarks which prints the amount of lines in file containing
the pattern.

### Example
Counting the number of lines in `README.md` with `cmake` on them.
```
$ ./buid/ca_cli lines 'cmake' README.md
3
```

## References
<a id="1">[1]</a>
Lukas Holik and Lenka Holikova and Juraj Sic and Tomas Vojnar [(2023)](https://www.fit.vut.cz/research/publication/12931/).
Fast Matching of Regular Patterns with Synchronizing Counting (Technical Report).

<a id="2">[2]</a>
[RE2](https://github.com/google/re2): A Regular Expressions library developed by Google.

