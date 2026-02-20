#include "tracelab/commands.h"
#include "tracelab/constants.h"

#include <iostream>
#include <string>
#include <vector>

namespace tracelab {

// Prints top-level CLI help and supported subcommands.
void PrintUsage() {
    std::cout
        << "TraceLab v" << kSchemaVersion << "\n"
        << "Usage:\n"
        << "  tracelab doctor [--json <path>]\n"
        << "  tracelab run [--native | --qemu <arch>] [--strict] [--json <path>] -- <command...>\n"
        << "  tracelab report <result.json>\n"
        << "  tracelab inspect [--json <path>] <binary>\n";
}

} // namespace tracelab

// Dispatches argv to the selected TraceLab subcommand handler.
int main(int argc, char **argv) {
    using namespace tracelab;

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string subcommand = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (subcommand == "--help" || subcommand == "-h" || subcommand == "help") {
        PrintUsage();
        return 0;
    }
    if (subcommand == "doctor") {
        return HandleDoctor(args);
    }
    if (subcommand == "run") {
        return HandleRun(args);
    }
    if (subcommand == "report") {
        return HandleReport(args);
    }
    if (subcommand == "inspect") {
        return HandleInspect(args);
    }

    std::cerr << "Unknown subcommand: " << subcommand << "\n";
    PrintUsage();
    return 1;
}
