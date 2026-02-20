#pragma once

#include <string>
#include <vector>

namespace tracelab {
    // Prints top-level CLI usage and available subcommands.
    void PrintUsage();

    // Executes `tracelab doctor`.
    // Returns process-style exit codes (0 success, non-zero on error/missing requirements).
    int HandleDoctor(const std::vector<std::string> &args);

    // Executes `tracelab run`.
    // Runs the workload and emits collector results and fallback counters.
    int HandleRun(const std::vector<std::string> &args);

    // Executes `tracelab report`.
    // Reads an existing run JSON artifact and renders a human summary.
    int HandleReport(const std::vector<std::string> &args);

    // Executes `tracelab inspect`.
    // Reads binary metadata (ISA/ABI/linkage/symbol hints) using host tooling.
    int HandleInspect(const std::vector<std::string> &args);

    // Executes `tracelab compare`.
    // Compares native and QEMU run artifacts and reports delta/throughput changes.
    int HandleCompare(const std::vector<std::string> &args);
} // namespace tracelab