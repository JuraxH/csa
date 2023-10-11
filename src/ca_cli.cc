#include "argparse.hpp"

#include "csa.hh"
#include "glushkov.hh"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <fstream>

using namespace std::string_literals;

void count_lines(std::string_view pattern, std::string file) {
    std::ifstream input(file);

    if (!input.is_open()) {
        std::cerr << "Failed to open file " << file << '\n';
        std::exit(1);
    }

    CSA::Matcher matcher(pattern);
    std::string line;
    unsigned matches = 0;
    while (getline(input, line)) {
        if (matcher.match(line)) {
            ++matches;
        }
    }

    std::cout << matches << std::endl;
}

void debug_ca(std::string_view pattern, bool print_graph) {
    auto ca = CA::glushkov::Builder::get_ca(pattern);
    if (print_graph) {
        std::cout << ca.to_DOT([] (uint8_t arg) { return std::to_string(arg); }) << std::endl;
    }
}

void debug_csa(std::string_view pattern, bool print_graph) {
    CSA::Visualizer vis(pattern);
    if (print_graph) {
        std::cout << vis.to_DOT_CSA() << std::endl;
    }
}

int main (int argc, char *argv[]) {
    argparse::ArgumentParser program("ca_cli");
    program.add_description("Run with one of the subcomands");

    argparse::ArgumentParser lines_command("lines");
    lines_command.add_description("Outputs the number of lines matching pattern");
    lines_command.add_argument("pattern")
        .help("regex using the RE2 syntax");
    lines_command.add_argument("file")
        .help("the file to be read");

    argparse::ArgumentParser debug_command("debug");
    debug_command.add_description("Prints the automaton in DOT format.");
    debug_command.add_argument("automaton")
        .help("ca or csa");
    debug_command.add_argument("pattern")
        .help("using the RE2 syntax");
    debug_command.add_argument("--check")
        .help("do not print graph")
        .default_value(false)
        .implicit_value(true);

    program.add_subparser(lines_command);
    program.add_subparser(debug_command);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        if (program.is_subcommand_used(lines_command)) {
            std::cerr << lines_command;
        } else if (program.is_subcommand_used(debug_command)) {
            std::cerr << debug_command;
        } else {
            std::cerr << program;
        }
        std::exit(1);
    }

    if (program.is_subcommand_used(lines_command)) {
        auto pattern = lines_command.get<std::string>("pattern");
        auto file_name = lines_command.get<std::string>("file");
        count_lines(pattern, std::move(file_name));
    } else if (program.is_subcommand_used(debug_command)) {
        auto pattern = debug_command.get<std::string>("pattern");
        auto automaton = debug_command.get<std::string>("automaton");
        bool print = debug_command["--check"] == false;
        if (automaton == "ca") {
            debug_ca(pattern, print);
        } else if (automaton == "csa") {
            debug_csa(pattern, print);
        } else {
            std::cerr << "automaton must be either ca or csa\n";
            std::exit(1);
        }
    } else {
        std::cerr << program;
    }

    return 0;
}
