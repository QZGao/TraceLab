#include "tracelab/diagnosis.h"
#include "tracelab/util.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace tracelab {
    namespace {
        // Formats a numeric value for report-friendly evidence strings.
        std::string FormatNumber(double value, int precision = 3) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(precision) << value;
            return out.str();
        }

        // Adds a limitation string if it is not already present.
        void AddUniqueLimitation(std::vector<std::string> *limitations, const std::string &text) {
            if (limitations == nullptr) {
                return;
            }
            for (const std::string &existing: *limitations) {
                if (existing == text) {
                    return;
                }
            }
            limitations->push_back(text);
        }

        // Returns true if syscall name is typically associated with filesystem I/O.
        bool IsIoSyscall(const std::string &name) {
            const std::string lower = ToLower(name);
            return lower == "read" || lower == "write" || lower == "pread64" || lower == "pwrite64" ||
                   lower == "preadv" || lower == "pwritev" || lower == "readv" || lower == "writev" ||
                   lower == "open" || lower == "openat" || lower == "close" || lower == "fsync" ||
                   lower == "fdatasync" || lower == "stat" || lower == "fstat" || lower == "lstat" ||
                   lower == "newfstatat" || lower == "getdents" || lower == "getdents64";
        }

        // Derived metrics used by diagnosis rules.
        struct DerivedMetrics {
            bool has_ipc = false;
            double ipc = 0.0;

            bool has_cache_miss_rate = false;
            double cache_miss_rate = 0.0;

            bool has_syscall_share = false;
            double syscall_share = 0.0;

            bool has_io_share = false;
            double io_share = 0.0;

            bool has_top_syscall_share = false;
            double top_syscall_share = 0.0;
            std::string top_syscall_name;
            double top_syscall_time_sec = 0.0;

            bool has_page_fault_rate = false;
            double page_fault_rate = 0.0;

            bool has_voluntary_switch_rate = false;
            double voluntary_switch_rate = 0.0;

            bool has_max_rss_mb = false;
            double max_rss_mb = 0.0;
        };

        // Computes reusable derived metrics from collector outputs.
        DerivedMetrics ComputeDerived(const WorkloadRunResult &workload, const PerfStatResult &perf,
                                      const StraceSummaryResult &strace) {
            DerivedMetrics derived;

            if (perf.status.status == "ok" && perf.data.has_cycles && perf.data.cycles > 0.0 &&
                perf.data.has_instructions) {
                derived.has_ipc = true;
                derived.ipc = perf.data.instructions / perf.data.cycles;
            }

            if (perf.status.status == "ok" && perf.data.has_cache_misses && perf.data.has_instructions &&
                perf.data.instructions > 0.0) {
                derived.has_cache_miss_rate = true;
                derived.cache_miss_rate = perf.data.cache_misses / perf.data.instructions;
            }

            if (strace.status.status == "ok" && strace.data.has_total_time && workload.wall_time_sec > 0.0) {
                derived.has_syscall_share = true;
                derived.syscall_share = strace.data.total_time_sec / workload.wall_time_sec;
            }

            if (!strace.data.entries.empty() && strace.data.has_total_time && strace.data.total_time_sec > 0.0) {
                double io_time_sec = 0.0;
                for (const StraceSyscallEntry &entry: strace.data.entries) {
                    if (IsIoSyscall(entry.name)) {
                        io_time_sec += std::max(0.0, entry.time_sec);
                    }
                }
                derived.has_io_share = true;
                derived.io_share = io_time_sec / strace.data.total_time_sec;

                const StraceSyscallEntry &top = strace.data.entries.front();
                derived.has_top_syscall_share = true;
                derived.top_syscall_name = top.name;
                derived.top_syscall_time_sec = top.time_sec;
                derived.top_syscall_share = top.time_sec / strace.data.total_time_sec;
            }

            if (perf.status.status == "ok" && perf.data.has_page_faults && workload.wall_time_sec > 0.0) {
                derived.has_page_fault_rate = true;
                derived.page_fault_rate = perf.data.page_faults / workload.wall_time_sec;
            }

            if (workload.proc_sample.has_voluntary_ctxt_switches && workload.wall_time_sec > 0.0) {
                derived.has_voluntary_switch_rate = true;
                derived.voluntary_switch_rate =
                        static_cast<double>(workload.proc_sample.voluntary_ctxt_switches) / workload.wall_time_sec;
            }

            if (workload.proc_sample.has_max_rss_kb) {
                derived.has_max_rss_mb = true;
                derived.max_rss_mb = static_cast<double>(workload.proc_sample.max_rss_kb) / 1024.0;
            }

            return derived;
        }

        // Adds availability-driven caveats that impact confidence.
        void PopulateLimitations(const WorkloadRunResult &workload, const PerfStatResult &perf,
                                 const StraceSummaryResult &strace, const std::string &mode,
                                 std::vector<std::string> *limitations) {
            if (mode == "qemu") {
                AddUniqueLimitation(
                    limitations,
                    "Perf counters captured under QEMU emulation; compare primarily by wall time and throughput.");
            }
            if (perf.status.status != "ok") {
                const std::string reason =
                        perf.status.reason.empty() ? perf.status.status : perf.status.reason;
                AddUniqueLimitation(limitations, "perf collector not fully usable: " + reason);
            }
            if (strace.status.status != "ok") {
                const std::string reason =
                        strace.status.reason.empty() ? strace.status.status : strace.status.reason;
                AddUniqueLimitation(limitations, "strace collector not fully usable: " + reason);
            }
            if (workload.proc_collector_status.status != "ok") {
                const std::string reason = workload.proc_collector_status.reason.empty()
                                               ? workload.proc_collector_status.status
                                               : workload.proc_collector_status.reason;
                AddUniqueLimitation(limitations, "proc status sampler not fully usable: " + reason);
            }
            if (workload.wall_time_sec > 0.0 && workload.wall_time_sec < 0.05) {
                AddUniqueLimitation(limitations, "Workload completed in under 50ms; startup noise may dominate.");
            }
        }

        // Ensures at least two evidence points are present, as required by v1 reporting.
        void EnsureMinimumEvidence(const WorkloadRunResult &workload, const PerfStatResult &perf,
                                   const StraceSummaryResult &strace,
                                   std::vector<DiagnosisEvidence> *evidence) {
            if (evidence == nullptr) {
                return;
            }
            if (evidence->size() >= 2) {
                return;
            }

            const auto push_unique = [&](const std::string &metric, const std::string &value,
                                         const std::string &detail) {
                for (const DiagnosisEvidence &item: *evidence) {
                    if (item.metric == metric) {
                        return;
                    }
                }
                evidence->push_back({metric, value, detail});
            };

            push_unique("wall_time_sec", FormatNumber(workload.wall_time_sec, 6),
                        "Elapsed runtime from fallback timer.");

            std::string collector_states = "perf=" + perf.status.status + ", strace=" + strace.status.status +
                                           ", proc=" + workload.proc_collector_status.status;
            push_unique("collector_statuses", collector_states,
                        "Collector availability influences diagnosis confidence.");
        }
    } // namespace

    // Computes a rule-based bottleneck diagnosis from available metrics.
    DiagnosisResult DiagnoseRun(const WorkloadRunResult &workload, const PerfStatResult &perf,
                                const StraceSummaryResult &strace, const std::string &mode) {
        DiagnosisResult result;
        const DerivedMetrics derived = ComputeDerived(workload, perf, strace);
        PopulateLimitations(workload, perf, strace, mode, &result.limitations);

        const bool memory_pressure =
                derived.has_max_rss_mb && derived.max_rss_mb >= 512.0 &&
                ((derived.has_page_fault_rate && derived.page_fault_rate >= 500.0) ||
                 (derived.has_voluntary_switch_rate && derived.voluntary_switch_rate >= 5000.0));

        if (memory_pressure) {
            result.label = "memory-pressure";
            result.confidence =
                    (derived.has_page_fault_rate && derived.page_fault_rate >= 2000.0) ? "high" : "medium";
            result.evidence.push_back({
                "max_rss_mb", FormatNumber(derived.max_rss_mb, 1),
                "Peak RSS sampled from /proc/<pid>/status."
            });
            if (derived.has_page_fault_rate) {
                result.evidence.push_back({
                    "page_faults_per_sec", FormatNumber(derived.page_fault_rate, 1),
                    "Page fault activity is elevated for this runtime."
                });
            }
            if (derived.has_voluntary_switch_rate) {
                result.evidence.push_back(
                    {
                        "voluntary_ctx_switches_per_sec", FormatNumber(derived.voluntary_switch_rate, 1),
                        "High switching can indicate stalls around memory activity."
                    });
            }
        } else if (derived.has_syscall_share && derived.has_io_share && derived.syscall_share >= 0.15 &&
                   derived.io_share >= 0.60) {
            result.label = "io-bound";
            result.confidence =
                    (derived.syscall_share >= 0.30 && derived.io_share >= 0.75) ? "high" : "medium";
            result.evidence.push_back({
                "syscall_time_share", FormatNumber(derived.syscall_share, 3),
                "Share of wall time spent inside syscalls."
            });
            result.evidence.push_back({
                "io_syscall_share", FormatNumber(derived.io_share, 3),
                "I/O-related syscalls dominate syscall time."
            });
            if (derived.has_top_syscall_share) {
                result.evidence.push_back(
                    {
                        "top_syscall",
                        derived.top_syscall_name + " (" + FormatNumber(derived.top_syscall_time_sec, 6) + "s)",
                        "Most expensive syscall entry in strace summary."
                    });
            }
        } else if (derived.has_syscall_share && derived.syscall_share >= 0.15) {
            result.label = "syscall-heavy";
            result.confidence = derived.syscall_share >= 0.35 ? "high" : "medium";
            result.evidence.push_back({
                "syscall_time_share", FormatNumber(derived.syscall_share, 3),
                "Share of wall time spent inside syscalls."
            });
            if (derived.has_top_syscall_share) {
                result.evidence.push_back({
                    "top_syscall_share", FormatNumber(derived.top_syscall_share, 3),
                    "Top syscall concentration within strace summary."
                });
                result.evidence.push_back(
                    {
                        "top_syscall",
                        derived.top_syscall_name + " (" + FormatNumber(derived.top_syscall_time_sec, 6) + "s)",
                        "Most expensive syscall entry in strace summary."
                    });
            }
        } else if (derived.has_ipc && derived.ipc >= 0.90 &&
                   (!derived.has_syscall_share || derived.syscall_share <= 0.10) &&
                   (!derived.has_cache_miss_rate || derived.cache_miss_rate <= 0.05)) {
            result.label = "cpu-bound";
            result.confidence =
                    (derived.ipc >= 1.20 && (!derived.has_syscall_share || derived.syscall_share <= 0.05))
                        ? "high"
                        : "medium";
            result.evidence.push_back({
                "ipc", FormatNumber(derived.ipc, 3),
                "Instructions per cycle from perf counters."
            });
            if (derived.has_syscall_share) {
                result.evidence.push_back({
                    "syscall_time_share", FormatNumber(derived.syscall_share, 3),
                    "Low syscall share suggests compute-heavy execution."
                });
            }
            if (derived.has_cache_miss_rate) {
                result.evidence.push_back({
                    "cache_miss_per_instruction",
                    FormatNumber(derived.cache_miss_rate, 6),
                    "Cache-miss density from perf counters."
                });
            }
        } else {
            result.label = "inconclusive";
            result.confidence = "low";
            result.evidence.push_back({
                "wall_time_sec", FormatNumber(workload.wall_time_sec, 6),
                "Elapsed runtime from fallback timer."
            });
            result.evidence.push_back({
                "exit_code", std::to_string(workload.exit_code),
                "Non-zero exit or limited telemetry can prevent clear attribution."
            });
            AddUniqueLimitation(&result.limitations,
                                "No rule crossed confidence thresholds for CPU, syscall, I/O, or memory pressure.");
        }

        EnsureMinimumEvidence(workload, perf, strace, &result.evidence);
        return result;
    }

    // Serializes diagnosis fields into the run artifact.
    std::string DiagnosisToJson(const DiagnosisResult &diagnosis, int indent) {
        const std::string pad(indent, ' ');
        const std::string pad2(indent + 2, ' ');
        const std::string pad4(indent + 4, ' ');
        std::ostringstream out;

        out << "{\n"
                << pad2 << "\"label\": \"" << JsonEscape(diagnosis.label) << "\",\n"
                << pad2 << "\"confidence\": \"" << JsonEscape(diagnosis.confidence) << "\",\n"
                << pad2 << "\"evidence\": [";

        for (size_t i = 0; i < diagnosis.evidence.size(); ++i) {
            if (i == 0) {
                out << "\n";
            }
            const DiagnosisEvidence &e = diagnosis.evidence[i];
            out << pad4 << "{\n"
                    << pad4 << "  \"metric\": \"" << JsonEscape(e.metric) << "\",\n"
                    << pad4 << "  \"value\": \"" << JsonEscape(e.value) << "\",\n"
                    << pad4 << "  \"detail\": \"" << JsonEscape(e.detail) << "\"\n"
                    << pad4 << "}";
            if (i + 1 < diagnosis.evidence.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << pad2 << "],\n"
                << pad2 << "\"limitations\": [";

        for (size_t i = 0; i < diagnosis.limitations.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "\"" << JsonEscape(diagnosis.limitations[i]) << "\"";
        }
        out << "]\n"
                << pad << "}";
        return out.str();
    }
} // namespace tracelab