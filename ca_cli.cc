#include "argparse.hpp"
#include <iostream>
#include <stdexcept>

int main (int argc, char *argv[]) {
    argparse::ArgumentParser program("ca_cli");
    program.add_description("Run with one of the subcomands");

    argparse::ArgumentParser lines_command("lines");
    lines_command.add_description("Outputs the number of lines matching pattern");

    argparse::ArgumentParser debug_command("debug");
    debug_command.add_description("Prints the automaton in DOT format.");

    debug_command.add_argument("automaton")
        .help("ca or csa");

    debug_command.add_argument("pattern")
        .help("using the RE2 syntax");


    lines_command.add_argument("pattern")
        .help("regex using the RE2 syntax");
    lines_command.add_argument("file")
        .help("the file to be read");

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
        // TODO: match_lines(pattern, file);
        std::cerr << "lines: " << pattern << " " << file_name << std::endl;
    } else if (program.is_subcommand_used(debug_command)) {
        auto pattern = debug_command.get<std::string>("pattern");
        auto automaton = debug_command.get<std::string>("automaton");
        if (automaton == "ca") {
            // TODO: print_ca(pattern)
            std::cerr << "printing the DOT graph of ca: " << pattern << std::endl;
        } else if (automaton == "csa") {
            // TODO: print_csa(pattern)
            std::cerr << "printing the DOT graph of csa: " << pattern << std::endl;
        } else {
            std::cerr << "automaton must be either ca or csa\n";
        }
    } else {
        std::cerr << program;
    }

    return 0;
}
