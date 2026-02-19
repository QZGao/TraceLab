#include "tracelab/collectors.h"
#include "tracelab/parsers.h"
#include "tracelab/util.h"

namespace tracelab {

StraceSummaryResult CollectStraceSummary(const std::vector<std::string> &command, int timeout_sec) {
    StraceSummaryResult result;

#ifndef __linux__
    (void)command;
    (void)timeout_sec;
    result.status = {"unavailable", "strace collector is Linux-only"};
    return result;
#else
    if (command.empty()) {
        result.status = {"error", "empty command"};
        return result;
    }
    if (!CommandExists("strace")) {
        result.status = {"unavailable", "strace not found in PATH"};
        return result;
    }

    std::string wrapped = "strace -qq -c -- " + JoinQuoted(command) + " 2>&1 1>/dev/null";
    const bool can_timeout = timeout_sec > 0 && CommandExists("timeout");
    if (can_timeout) {
        wrapped = "timeout --signal=KILL " + std::to_string(timeout_sec) + "s " + wrapped;
    }

    const CommandResult run = RunCommandCapture(wrapped);
    result.command_exit_code = run.exit_code;
    result.raw_output = run.output;
    result.timed_out = (can_timeout && run.exit_code == 124);

    const bool parsed = ParseStraceSummaryOutput(run.output, &result.data);
    if (result.timed_out) {
        result.status = {"error", "strace collector timed out"};
    } else if (parsed) {
        result.status = {"ok", ""};
    } else if (run.exit_code == 0) {
        result.status = {"error", "strace output missing expected summary rows"};
    } else {
        result.status =
            {"error", "strace command failed with exit code " + std::to_string(run.exit_code)};
    }

    return result;
#endif
}

} // namespace tracelab
