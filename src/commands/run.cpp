#include "tracelab/collectors.h"
#include "tracelab/commands.h"
#include "tracelab/constants.h"
#include "tracelab/util.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace tracelab {

namespace {

// Emits JSON boolean literals without pulling in a JSON library.
std::string JsonBool(bool value) {
    return value ? "true" : "false";
}

// Emits either an integer value or JSON null for optional fields.
std::string JsonIntOrNull(bool has_value, long long value) {
    if (!has_value) {
        return "null";
    }
    return std::to_string(value);
}

// Appends shared collector status fields at a fixed indentation level.
void AppendCollectorStatusFields(std::ostringstream &out, const CollectorStatus &status, int indent) {
    const std::string pad(indent, ' ');
    out << pad << "\"status\": \"" << JsonEscape(status.status) << "\"";
    if (!status.reason.empty()) {
        out << ",\n" << pad << "\"reason\": \"" << JsonEscape(status.reason) << "\"";
    }
}

// Serializes `perf stat` collector output into the run-result JSON shape.
std::string PerfCollectorToJson(const PerfStatResult &perf) {
    std::ostringstream out;
    out << "{\n";
    AppendCollectorStatusFields(out, perf.status, 6);
    out << ",\n"
        << "      \"command_exit_code\": " << perf.command_exit_code << ",\n"
        << "      \"timed_out\": " << JsonBool(perf.timed_out) << ",\n"
        << "      \"counters\": {\n";

    bool first = true;
    auto emit_counter = [&](const char *name, bool has, double value) {
        if (!has) {
            return;
        }
        if (!first) {
            out << ",\n";
        }
        out << "        \"" << name << "\": " << std::fixed << std::setprecision(0) << value;
        first = false;
    };
    emit_counter("cycles", perf.data.has_cycles, perf.data.cycles);
    emit_counter("instructions", perf.data.has_instructions, perf.data.instructions);
    emit_counter("branches", perf.data.has_branches, perf.data.branches);
    emit_counter("branch_misses", perf.data.has_branch_misses, perf.data.branch_misses);
    emit_counter("cache_misses", perf.data.has_cache_misses, perf.data.cache_misses);
    emit_counter("page_faults", perf.data.has_page_faults, perf.data.page_faults);

    if (!first) {
        out << "\n";
    }
    out << "      }\n"
        << "    }";
    return out.str();
}

// Serializes `strace -c` collector output, keeping the top syscall rows.
std::string StraceCollectorToJson(const StraceSummaryResult &strace) {
    std::ostringstream out;
    out << "{\n";
    AppendCollectorStatusFields(out, strace.status, 6);
    out << ",\n"
        << "      \"command_exit_code\": " << strace.command_exit_code << ",\n"
        << "      \"timed_out\": " << JsonBool(strace.timed_out) << ",\n"
        << "      \"top_syscalls\": [";

    const size_t limit = std::min<size_t>(strace.data.entries.size(), 15);
    if (limit > 0) {
        out << "\n";
    }
    for (size_t i = 0; i < limit; ++i) {
        const StraceSyscallEntry &entry = strace.data.entries[i];
        out << "        {\n"
            << "          \"name\": \"" << JsonEscape(entry.name) << "\",\n"
            << "          \"calls\": " << entry.calls << ",\n"
            << "          \"time_sec\": " << std::fixed << std::setprecision(6) << entry.time_sec
            << ",\n"
            << "          \"errors\": " << entry.errors << "\n"
            << "        }";
        if (i + 1 < limit) {
            out << ",";
        }
        out << "\n";
    }
    out << "      ]";
    if (strace.data.has_total_time) {
        out << ",\n"
            << "      \"total_time_sec\": " << std::fixed << std::setprecision(6)
            << strace.data.total_time_sec;
    }
    out << "\n"
        << "    }";
    return out.str();
}

// Serializes `/proc/<pid>/status` fallback sampling fields.
std::string ProcCollectorToJson(const WorkloadRunResult &workload) {
    std::ostringstream out;
    out << "{\n";
    AppendCollectorStatusFields(out, workload.proc_collector_status, 6);
    out << ",\n"
        << "      \"max_rss_kb\": "
        << JsonIntOrNull(workload.proc_sample.has_max_rss_kb, workload.proc_sample.max_rss_kb)
        << ",\n"
        << "      \"voluntary_ctxt_switches\": "
        << JsonIntOrNull(workload.proc_sample.has_voluntary_ctxt_switches,
                         workload.proc_sample.voluntary_ctxt_switches)
        << ",\n"
        << "      \"nonvoluntary_ctxt_switches\": "
        << JsonIntOrNull(workload.proc_sample.has_nonvoluntary_ctxt_switches,
                         workload.proc_sample.nonvoluntary_ctxt_switches)
        << "\n"
        << "    }";
    return out.str();
}

// Strict mode currently accepts only fully successful collectors.
bool IsCollectorUsableInStrictMode(const CollectorStatus &status) {
    return status.status == "ok";
}

} // namespace

// Implements `tracelab run`: execute workload, run collectors, emit report JSON.
int HandleRun(const std::vector<std::string> &args) {
    std::string mode = "native";
    std::string qemu_arch;
    std::string json_path;
    bool strict = false;
    int collector_timeout_sec = 120;

    // Split CLI args into TraceLab options and workload argv (`--` separator).
    size_t separator = args.size();
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--") {
            separator = i;
            break;
        }
    }

    if (separator == args.size()) {
        std::cerr << "run: missing workload separator '--'\n";
        return 2;
    }
    if (separator + 1 >= args.size()) {
        std::cerr << "run: missing workload command after '--'\n";
        return 2;
    }

    // Parse TraceLab options before the workload separator.
    for (size_t i = 0; i < separator; ++i) {
        const std::string &arg = args[i];
        if (arg == "--native") {
            mode = "native";
            qemu_arch.clear();
        } else if (arg == "--qemu") {
            if (i + 1 >= separator) {
                std::cerr << "run: --qemu expects an architecture\n";
                return 2;
            }
            mode = "qemu";
            qemu_arch = args[++i];
        } else if (arg == "--json") {
            if (i + 1 >= separator) {
                std::cerr << "run: --json expects a path\n";
                return 2;
            }
            json_path = args[++i];
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--collector-timeout-sec") {
            if (i + 1 >= separator) {
                std::cerr << "run: --collector-timeout-sec expects a positive integer\n";
                return 2;
            }
            try {
                collector_timeout_sec = std::stoi(args[++i]);
            } catch (...) {
                std::cerr << "run: invalid timeout value\n";
                return 2;
            }
            if (collector_timeout_sec <= 0) {
                std::cerr << "run: --collector-timeout-sec must be > 0\n";
                return 2;
            }
        } else if (arg == "--help") {
            std::cout
                << "Usage: tracelab run [--native | --qemu <arch>] [--strict] [--json <path>] "
                   "[--collector-timeout-sec <N>] -- <command...>\n";
            return 0;
        } else {
            std::cerr << "run: unknown argument: " << arg << "\n";
            return 2;
        }
    }

    // Forward everything after `--` to the workload.
    std::vector<std::string> workload_args;
    for (size_t i = separator + 1; i < args.size(); ++i) {
        workload_args.push_back(args[i]);
    }
    if (workload_args.empty()) {
        std::cerr << "run: empty workload command\n";
        return 2;
    }

    // Compose the executable argv (optionally prefixed with qemu-<arch>).
    std::vector<std::string> exec_args = workload_args;
    if (mode == "qemu") {
        if (qemu_arch.empty()) {
            std::cerr << "run: qemu mode requires an architecture\n";
            return 2;
        }
        const std::string qemu_bin = "qemu-" + qemu_arch;
        if (!CommandExists(qemu_bin)) {
            std::cerr << "run: missing " << qemu_bin << " in PATH\n";
            return 2;
        }
        exec_args.insert(exec_args.begin(), qemu_bin);
    }

    // In strict mode, fail early if required Linux collectors are unavailable.
    if (strict) {
        if (HostOs() != "linux") {
            std::cerr << "run: strict mode requires Linux collectors\n";
            return 2;
        }
        if (!CommandExists("perf") || !CommandExists("strace")) {
            std::cerr << "run: strict mode requires perf and strace in PATH\n";
            return 2;
        }
    }

    // Current strategy: run workload once for fallback/proc signals, then replay for
    // tool-driven collectors (perf/strace) so each collector can manage its own runtime options.
    const WorkloadRunResult workload = RunWithProcSampling(exec_args);
    const PerfStatResult perf = CollectPerfStat(exec_args, collector_timeout_sec);
    const StraceSummaryResult strace = CollectStraceSummary(exec_args, collector_timeout_sec);

    // Strict mode treats any non-ok collector status as a hard failure.
    if (strict &&
        (!IsCollectorUsableInStrictMode(workload.proc_collector_status) ||
         !IsCollectorUsableInStrictMode(perf.status) ||
         !IsCollectorUsableInStrictMode(strace.status))) {
        std::cerr << "run: strict mode failed because at least one collector was not usable\n";
        return 2;
    }

    const std::string user_command = JoinRaw(workload_args);
    const std::string exec_command = JoinQuoted(exec_args);

    std::ostringstream duration;
    duration << std::fixed << std::setprecision(6) << workload.wall_time_sec;

    // Emit a deterministic JSON artifact for CI and post-processing.
    std::ostringstream json;
    json << "{\n"
         << "  \"schema_version\": \"" << kSchemaVersion << "\",\n"
         << "  \"kind\": \"run_result\",\n"
         << "  \"timestamp_utc\": \"" << NowUtcIso8601() << "\",\n"
         << "  \"mode\": \"" << mode << "\",\n"
         << "  \"collection_strategy\": \"main_run_plus_replay_collectors\",\n"
         << "  \"collector_timeout_sec\": " << collector_timeout_sec << ",\n"
         << "  \"command\": \"" << JsonEscape(user_command) << "\",\n"
         << "  \"exec_command\": \"" << JsonEscape(exec_command) << "\",\n"
         << "  \"duration_sec\": " << duration.str() << ",\n"
         << "  \"exit_code\": " << workload.exit_code << ",\n"
         << "  \"strict\": " << JsonBool(strict) << ",\n"
         << "  \"fallback\": {\n"
         << "    \"wall_time_sec\": " << duration.str() << ",\n"
         << "    \"exit_classification\": \"" << JsonEscape(workload.exit_classification) << "\",\n"
         << "    \"max_rss_kb\": "
         << JsonIntOrNull(workload.proc_sample.has_max_rss_kb, workload.proc_sample.max_rss_kb)
         << ",\n"
         << "    \"voluntary_ctxt_switches\": "
         << JsonIntOrNull(workload.proc_sample.has_voluntary_ctxt_switches,
                          workload.proc_sample.voluntary_ctxt_switches)
         << ",\n"
         << "    \"nonvoluntary_ctxt_switches\": "
         << JsonIntOrNull(workload.proc_sample.has_nonvoluntary_ctxt_switches,
                          workload.proc_sample.nonvoluntary_ctxt_switches)
         << "\n"
         << "  },\n";

    if (mode == "qemu") {
        json << "  \"qemu\": {\n"
             << "    \"arch\": \"" << JsonEscape(qemu_arch) << "\"\n"
             << "  },\n";
    }

    json << "  \"host\": {\n"
         << "    \"os\": \"" << HostOs() << "\",\n"
         << "    \"arch\": \"" << HostArch() << "\",\n"
         << "    \"git_sha\": \"" << JsonEscape(DetectGitSha()) << "\"\n"
         << "  },\n"
         << "  \"collectors\": {\n"
         << "    \"perf_stat\": " << PerfCollectorToJson(perf) << ",\n"
         << "    \"strace_summary\": " << StraceCollectorToJson(strace) << ",\n"
         << "    \"proc_status\": " << ProcCollectorToJson(workload) << "\n"
         << "  }\n"
         << "}\n";

    std::cout << "TraceLab Run\n";
    std::cout << "  Mode: " << mode << "\n";
    std::cout << "  Command: " << user_command << "\n";
    std::cout << "  Duration: " << duration.str() << "s\n";
    std::cout << "  Exit code: " << workload.exit_code << " (" << workload.exit_classification << ")\n";
    if (workload.proc_sample.has_max_rss_kb) {
        std::cout << "  Fallback max RSS: " << workload.proc_sample.max_rss_kb << " kB\n";
    }
    if (workload.proc_sample.has_voluntary_ctxt_switches ||
        workload.proc_sample.has_nonvoluntary_ctxt_switches) {
        std::cout << "  Fallback context switches: voluntary="
                  << (workload.proc_sample.has_voluntary_ctxt_switches
                          ? std::to_string(workload.proc_sample.voluntary_ctxt_switches)
                          : "n/a")
                  << ", nonvoluntary="
                  << (workload.proc_sample.has_nonvoluntary_ctxt_switches
                          ? std::to_string(workload.proc_sample.nonvoluntary_ctxt_switches)
                          : "n/a")
                  << "\n";
    }
    std::cout << "  Collector perf_stat: " << perf.status.status << "\n";
    std::cout << "  Collector strace_summary: " << strace.status.status << "\n";
    std::cout << "  Collector proc_status: " << workload.proc_collector_status.status << "\n";

    if (!json_path.empty()) {
        std::string error;
        if (!WriteTextFile(json_path, json.str(), &error)) {
            std::cerr << "run: failed to write " << json_path << ": " << error << "\n";
            return 2;
        }
        std::cout << "  JSON: " << json_path << "\n";
    }

    return workload.exit_code;
}

} // namespace tracelab
