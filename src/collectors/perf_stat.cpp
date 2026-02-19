#include "tracelab/collectors.h"
#include "tracelab/parsers.h"
#include "tracelab/util.h"

namespace tracelab {

PerfStatResult CollectPerfStat(const std::vector<std::string> &command, int timeout_sec) {
    PerfStatResult result;

#ifndef __linux__
    (void)command;
    (void)timeout_sec;
    result.status = {"unavailable", "perf collector is Linux-only"};
    return result;
#else
    if (command.empty()) {
        result.status = {"error", "empty command"};
        return result;
    }
    if (!CommandExists("perf")) {
        result.status = {"unavailable", "perf not found in PATH"};
        return result;
    }

    std::string wrapped =
        "perf stat -x, -e cycles,instructions,branches,branch-misses,cache-misses,page-faults -- " +
        JoinQuoted(command) + " 2>&1 1>/dev/null";

    const bool can_timeout = timeout_sec > 0 && CommandExists("timeout");
    if (can_timeout) {
        wrapped = "timeout --signal=KILL " + std::to_string(timeout_sec) + "s " + wrapped;
    }

    const CommandResult run = RunCommandCapture(wrapped);
    result.command_exit_code = run.exit_code;
    result.raw_output = run.output;
    result.timed_out = (can_timeout && run.exit_code == 124);

    const bool parsed = ParsePerfStatCsvOutput(run.output, &result.data);
    if (result.timed_out) {
        result.status = {"error", "perf collector timed out"};
    } else if (parsed) {
        result.status = {"ok", ""};
    } else if (run.exit_code == 0) {
        result.status = {"error", "perf output missing expected counters"};
    } else {
        result.status =
            {"error", "perf command failed with exit code " + std::to_string(run.exit_code)};
    }

    return result;
#endif
}

} // namespace tracelab
