#include "tracelab/commands.h"
#include "tracelab/util.h"

#include <iomanip>
#include <iostream>

namespace tracelab {

int HandleReport(const std::vector<std::string> &args) {
    if (args.empty() || args[0] == "--help") {
        std::cout << "Usage: tracelab report <result.json>\n";
        return args.empty() ? 2 : 0;
    }
    if (args.size() != 1) {
        std::cerr << "report: expected exactly one argument\n";
        return 2;
    }

    const std::string path = args[0];
    const auto content = ReadTextFile(path);
    if (!content.has_value()) {
        std::cerr << "report: failed to read " << path << "\n";
        return 2;
    }

    const auto kind = ExtractJsonString(content.value(), "kind");
    if (!kind.has_value() || kind.value() != "run_result") {
        std::cerr << "report: unsupported or missing kind field in " << path << "\n";
        return 2;
    }

    const std::string mode = ExtractJsonString(content.value(), "mode").value_or("unknown");
    const std::string command = ExtractJsonString(content.value(), "command").value_or("unknown");
    const auto duration = ExtractJsonNumber(content.value(), "duration_sec");
    const auto exit_code = ExtractJsonInteger(content.value(), "exit_code");
    const std::string perf =
        ExtractCollectorStatus(content.value(), "perf_stat").value_or("unknown");
    const std::string strace =
        ExtractCollectorStatus(content.value(), "strace_summary").value_or("unknown");
    const std::string proc =
        ExtractCollectorStatus(content.value(), "proc_status").value_or("unknown");

    std::cout << "TraceLab Report\n";
    std::cout << "  Source: " << path << "\n";
    std::cout << "  Mode: " << mode << "\n";
    std::cout << "  Command: " << command << "\n";
    if (duration.has_value()) {
        std::cout << "  Duration: " << std::fixed << std::setprecision(6) << duration.value()
                  << "s\n";
    } else {
        std::cout << "  Duration: unknown\n";
    }
    if (exit_code.has_value()) {
        std::cout << "  Exit code: " << exit_code.value() << "\n";
    } else {
        std::cout << "  Exit code: unknown\n";
    }
    std::cout << "  Collectors: perf_stat=" << perf << ", strace_summary=" << strace
              << ", proc_status=" << proc << "\n";

    return 0;
}

} // namespace tracelab
