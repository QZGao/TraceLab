#pragma once

#include "tracelab/util.h"

#include <string>
#include <vector>

namespace tracelab {

// Parsed subset of `perf stat` counters used by TraceLab v1.
struct PerfStatData {
    bool has_cycles = false;
    double cycles = 0.0;

    bool has_instructions = false;
    double instructions = 0.0;

    bool has_branches = false;
    double branches = 0.0;

    bool has_branch_misses = false;
    double branch_misses = 0.0;

    bool has_cache_misses = false;
    double cache_misses = 0.0;

    bool has_page_faults = false;
    double page_faults = 0.0;
};

// Result envelope for a perf collection attempt.
// `command_exit_code` is from the collector command itself, not the main workload run.
struct PerfStatResult {
    CollectorStatus status;
    int command_exit_code = -1;
    bool timed_out = false;
    PerfStatData data;
    std::string raw_output;
};

// Single syscall row from `strace -c` summary output.
struct StraceSyscallEntry {
    std::string name;
    long long calls = 0;
    double time_sec = 0.0;
    long long errors = 0;
};

// Parsed `strace -c` summary (top rows + total).
struct StraceSummaryData {
    std::vector<StraceSyscallEntry> entries;
    double total_time_sec = 0.0;
    bool has_total_time = false;
};

// Result envelope for a strace collection attempt.
struct StraceSummaryResult {
    CollectorStatus status;
    int command_exit_code = -1;
    bool timed_out = false;
    StraceSummaryData data;
    std::string raw_output;
};

// Snapshot values extracted from `/proc/<pid>/status` during a run.
struct ProcStatusSample {
    bool has_max_rss_kb = false;
    long long max_rss_kb = 0;

    bool has_voluntary_ctxt_switches = false;
    long long voluntary_ctxt_switches = 0;

    bool has_nonvoluntary_ctxt_switches = false;
    long long nonvoluntary_ctxt_switches = 0;
};

// Result for the primary workload execution with fallback metrics.
struct WorkloadRunResult {
    int exit_code = -1;
    std::string exit_classification = "unknown";
    double wall_time_sec = 0.0;
    ProcStatusSample proc_sample;
    CollectorStatus proc_collector_status;
};

// Executes a workload while sampling `/proc/<pid>/status` (Linux).
// On non-Linux hosts this degrades gracefully and still runs the command.
WorkloadRunResult RunWithProcSampling(const std::vector<std::string> &command);

// Replays the workload under `perf stat` and parses selected counters.
PerfStatResult CollectPerfStat(const std::vector<std::string> &command, int timeout_sec);

// Replays the workload under `strace -c` and parses syscall summary data.
StraceSummaryResult CollectStraceSummary(const std::vector<std::string> &command, int timeout_sec);

} // namespace tracelab
