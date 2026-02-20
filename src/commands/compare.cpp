#include "tracelab/commands.h"
#include "tracelab/constants.h"
#include "tracelab/qemu.h"
#include "tracelab/util.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tracelab {
    namespace {
        // Parsed subset of a run artifact needed for native-vs-qemu comparison.
        struct RunSample {
            std::string path;
            std::string mode;
            std::string command;
            double duration_sec = 0.0;

            std::optional<std::string> qemu_arch;

            std::string perf_status = "unknown";
            std::string strace_status = "unknown";
            std::string proc_status = "unknown";

            std::map<std::string, double> perf_counters;
        };

        // Joins string values with commas for user-facing messages.
        std::string JoinCommaSeparated(const std::vector<std::string> &values) {
            std::ostringstream out;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << values[i];
            }
            return out.str();
        }

        // Returns median value for a non-empty numeric vector.
        double Median(std::vector<double> values) {
            std::sort(values.begin(), values.end());
            const size_t n = values.size();
            if (n % 2 == 1) {
                return values[n / 2];
            }
            return (values[n / 2 - 1] + values[n / 2]) / 2.0;
        }

        // Reads and validates a run_result artifact into comparison-friendly fields.
        bool LoadRunSample(const std::string &path, const std::optional<std::string> &expected_mode,
                           RunSample *sample, std::string *error) {
            if (sample == nullptr) {
                if (error != nullptr) {
                    *error = "internal error: null sample pointer";
                }
                return false;
            }

            const auto text = ReadTextFile(path);
            if (!text.has_value()) {
                if (error != nullptr) {
                    *error = "unable to read artifact file";
                }
                return false;
            }

            const auto kind = ExtractJsonString(text.value(), "kind");
            if (!kind.has_value() || kind.value() != "run_result") {
                if (error != nullptr) {
                    *error = "artifact is not a run_result JSON";
                }
                return false;
            }

            const auto mode = ExtractJsonString(text.value(), "mode");
            const auto command = ExtractJsonString(text.value(), "command");
            const auto duration = ExtractJsonNumber(text.value(), "duration_sec");
            if (!mode.has_value() || !command.has_value() || !duration.has_value()) {
                if (error != nullptr) {
                    *error = "artifact missing one of required fields: mode, command, duration_sec";
                }
                return false;
            }

            if (expected_mode.has_value() && mode.value() != expected_mode.value()) {
                if (error != nullptr) {
                    *error = "expected mode '" + expected_mode.value() + "' but got '" + mode.value() + "'";
                }
                return false;
            }

            sample->path = path;
            sample->mode = mode.value();
            sample->command = command.value();
            sample->duration_sec = duration.value();
            sample->perf_status = ExtractCollectorStatus(text.value(), "perf_stat").value_or("unknown");
            sample->strace_status = ExtractCollectorStatus(text.value(), "strace_summary").value_or("unknown");
            sample->proc_status = ExtractCollectorStatus(text.value(), "proc_status").value_or("unknown");

            static const std::vector<std::string> kCounters = {
                "cycles", "instructions", "branches",
                "branch_misses", "cache_misses", "page_faults"
            };
            for (const std::string &counter: kCounters) {
                const auto value = ExtractJsonNumber(text.value(), counter);
                if (value.has_value()) {
                    sample->perf_counters[counter] = value.value();
                }
            }

            if (sample->mode == "qemu") {
                const std::regex qemu_arch_pattern(
                    "\"qemu\"\\s*:\\s*\\{[\\s\\S]*?\"arch\"\\s*:\\s*\"([^\"]+)\"");
                std::smatch arch_match;
                if (!std::regex_search(text.value(), arch_match, qemu_arch_pattern) || arch_match.size() < 2) {
                    if (error != nullptr) {
                        *error = "qemu run artifact missing qemu.arch";
                    }
                    return false;
                }
                const std::string raw_arch = arch_match[1].str();

                const auto normalized = NormalizeQemuArchSelector(raw_arch);
                if (!normalized.has_value()) {
                    if (error != nullptr) {
                        *error = "unsupported qemu arch '" + raw_arch + "' in artifact; supported: " +
                                 JoinCommaSeparated(SupportedQemuArchSelectors());
                    }
                    return false;
                }
                sample->qemu_arch = normalized.value();
            }

            return true;
        }

        // Returns true when a sample has any collector state other than "ok".
        bool HasAnyNonOkCollector(const RunSample &sample) {
            return sample.perf_status != "ok" || sample.strace_status != "ok" || sample.proc_status != "ok";
        }

        // Serializes a JSON array of strings.
        std::string JsonStringArray(const std::vector<std::string> &values) {
            std::ostringstream out;
            out << "[";
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << "\"" << JsonEscape(values[i]) << "\"";
            }
            out << "]";
            return out.str();
        }

        // Serializes counter ratios as a JSON object.
        std::string JsonCounterRatioObject(const std::map<std::string, double> &ratios) {
            std::ostringstream out;
            out << "{";
            bool first = true;
            for (const auto &kv: ratios) {
                if (!first) {
                    out << ", ";
                }
                out << "\"" << kv.first << "\": " << std::fixed << std::setprecision(6) << kv.second;
                first = false;
            }
            out << "}";
            return out.str();
        }
    } // namespace

    // Implements `tracelab compare`: compare native and qemu run artifacts.
    int HandleCompare(const std::vector<std::string> &args) {
        std::vector<std::string> native_paths;
        std::vector<std::string> qemu_paths;
        std::vector<std::string> positional_paths;
        std::string json_path;

        for (size_t i = 0; i < args.size(); ++i) {
            const std::string &arg = args[i];
            if (arg == "--native") {
                if (i + 1 >= args.size()) {
                    std::cerr << "compare: --native expects a run_result path\n";
                    return 2;
                }
                native_paths.push_back(args[++i]);
            } else if (arg == "--qemu") {
                if (i + 1 >= args.size()) {
                    std::cerr << "compare: --qemu expects a run_result path\n";
                    return 2;
                }
                qemu_paths.push_back(args[++i]);
            } else if (arg == "--json") {
                if (i + 1 >= args.size()) {
                    std::cerr << "compare: --json expects a path\n";
                    return 2;
                }
                json_path = args[++i];
            } else if (arg == "--help") {
                std::cout
                        << "Usage: tracelab compare [--native <result.json> ... --qemu <result.json> ...] "
                        "[--json <path>] [<native_result.json> <qemu_result.json>]\n";
                return 0;
            } else if (StartsWith(arg, "--")) {
                std::cerr << "compare: unknown argument: " << arg << "\n";
                return 2;
            } else {
                positional_paths.push_back(arg);
            }
        }

        if ((!native_paths.empty() || !qemu_paths.empty()) && !positional_paths.empty()) {
            std::cerr << "compare: use either positional mode (2 files) or --native/--qemu lists, not both\n";
            return 2;
        }

        std::vector<RunSample> native_samples;
        std::vector<RunSample> qemu_samples;

        if (!native_paths.empty() || !qemu_paths.empty()) {
            if (native_paths.empty() || qemu_paths.empty()) {
                std::cerr << "compare: both --native and --qemu inputs are required\n";
                return 2;
            }

            for (const std::string &path: native_paths) {
                RunSample sample;
                std::string error;
                if (!LoadRunSample(path, std::string("native"), &sample, &error)) {
                    std::cerr << "compare: failed to parse native artifact " << path << ": " << error << "\n";
                    return 2;
                }
                native_samples.push_back(sample);
            }
            for (const std::string &path: qemu_paths) {
                RunSample sample;
                std::string error;
                if (!LoadRunSample(path, std::string("qemu"), &sample, &error)) {
                    std::cerr << "compare: failed to parse qemu artifact " << path << ": " << error << "\n";
                    return 2;
                }
                qemu_samples.push_back(sample);
            }
        } else {
            if (positional_paths.size() != 2) {
                std::cerr << "compare: expected either two positional files or explicit --native/--qemu lists\n";
                return 2;
            }

            RunSample first;
            RunSample second;
            std::string first_error;
            std::string second_error;
            if (!LoadRunSample(positional_paths[0], std::nullopt, &first, &first_error)) {
                std::cerr << "compare: failed to parse artifact " << positional_paths[0] << ": " << first_error
                        << "\n";
                return 2;
            }
            if (!LoadRunSample(positional_paths[1], std::nullopt, &second, &second_error)) {
                std::cerr << "compare: failed to parse artifact " << positional_paths[1] << ": "
                        << second_error << "\n";
                return 2;
            }

            if (first.mode == "native" && second.mode == "qemu") {
                native_samples.push_back(first);
                qemu_samples.push_back(second);
            } else if (first.mode == "qemu" && second.mode == "native") {
                native_samples.push_back(second);
                qemu_samples.push_back(first);
            } else {
                std::cerr << "compare: positional inputs must include exactly one native and one qemu artifact\n";
                return 2;
            }
        }

        std::vector<double> native_durations;
        std::vector<double> qemu_durations;
        native_durations.reserve(native_samples.size());
        qemu_durations.reserve(qemu_samples.size());
        for (const RunSample &sample: native_samples) {
            native_durations.push_back(sample.duration_sec);
        }
        for (const RunSample &sample: qemu_samples) {
            qemu_durations.push_back(sample.duration_sec);
        }

        const double native_median = Median(native_durations);
        const double qemu_median = Median(qemu_durations);
        if (native_median <= 0.0 || qemu_median <= 0.0) {
            std::cerr << "compare: duration medians must be positive\n";
            return 2;
        }

        const double delta_duration_sec = qemu_median - native_median;
        const double slowdown_factor = qemu_median / native_median;
        const double throughput_ratio_qemu_vs_native = native_median / qemu_median;
        const double throughput_change_pct_qemu_vs_native =
                (throughput_ratio_qemu_vs_native - 1.0) * 100.0;

        std::set<std::string> qemu_archs;
        for (const RunSample &sample: qemu_samples) {
            if (sample.qemu_arch.has_value()) {
                qemu_archs.insert(sample.qemu_arch.value());
            }
        }
        const std::vector<std::string> qemu_arch_list(qemu_archs.begin(), qemu_archs.end());

        const std::string baseline_command = native_samples.front().command;
        bool commands_match = true;
        for (const RunSample &sample: native_samples) {
            if (sample.command != baseline_command) {
                commands_match = false;
                break;
            }
        }
        if (commands_match) {
            for (const RunSample &sample: qemu_samples) {
                if (sample.command != baseline_command) {
                    commands_match = false;
                    break;
                }
            }
        }

        const std::vector<std::string> counter_names = {
            "cycles", "instructions", "branches",
            "branch_misses", "cache_misses", "page_faults"
        };
        std::map<std::string, double> counter_ratios;
        for (const std::string &counter_name: counter_names) {
            std::vector<double> native_values;
            std::vector<double> qemu_values;
            for (const RunSample &sample: native_samples) {
                const auto it = sample.perf_counters.find(counter_name);
                if (it != sample.perf_counters.end()) {
                    native_values.push_back(it->second);
                }
            }
            for (const RunSample &sample: qemu_samples) {
                const auto it = sample.perf_counters.find(counter_name);
                if (it != sample.perf_counters.end()) {
                    qemu_values.push_back(it->second);
                }
            }
            if (native_values.empty() || qemu_values.empty()) {
                continue;
            }

            const double native_counter_median = Median(native_values);
            const double qemu_counter_median = Median(qemu_values);
            if (native_counter_median > 0.0) {
                counter_ratios[counter_name] = qemu_counter_median / native_counter_median;
            }
        }

        std::vector<std::string> caveats;
        caveats.push_back("Wall-clock and throughput are primary metrics for native vs QEMU comparison.");
        if (native_samples.size() != 5 || qemu_samples.size() != 5) {
            caveats.push_back("Protocol note: Section 4 recommends 1 warm-up plus 5 measured runs per mode; "
                              "provided native=" +
                              std::to_string(native_samples.size()) + ", qemu=" +
                              std::to_string(qemu_samples.size()) + ".");
        }

        bool qemu_perf_ok = false;
        for (const RunSample &sample: qemu_samples) {
            if (sample.perf_status == "ok") {
                qemu_perf_ok = true;
                break;
            }
        }
        if (qemu_perf_ok) {
            caveats.push_back(
                "Perf counters in QEMU mode are emulation-affected and not directly equivalent to native counters.");
        }
        if (!commands_match) {
            caveats.push_back("Input artifacts do not share an identical command string.");
        }

        bool native_non_ok_collectors = false;
        for (const RunSample &sample: native_samples) {
            if (HasAnyNonOkCollector(sample)) {
                native_non_ok_collectors = true;
                break;
            }
        }
        bool qemu_non_ok_collectors = false;
        for (const RunSample &sample: qemu_samples) {
            if (HasAnyNonOkCollector(sample)) {
                qemu_non_ok_collectors = true;
                break;
            }
        }
        if (native_non_ok_collectors || qemu_non_ok_collectors) {
            caveats.push_back("At least one collector was not 'ok' in the compared artifacts.");
        }
        if (qemu_arch_list.size() > 1) {
            caveats.push_back("Compared QEMU samples include multiple target architectures: " +
                              JoinCommaSeparated(qemu_arch_list) + ".");
        }

        std::cout << "TraceLab Compare\n";
        std::cout << "  Native samples: " << native_samples.size() << "\n";
        std::cout << "  QEMU samples: " << qemu_samples.size() << "\n";
        std::cout << "  Native median duration: " << std::fixed << std::setprecision(6) << native_median
                << "s\n";
        std::cout << "  QEMU median duration: " << std::fixed << std::setprecision(6) << qemu_median << "s\n";
        std::cout << "  Delta duration (qemu-native): " << std::fixed << std::setprecision(6)
                << delta_duration_sec << "s\n";
        std::cout << "  Slowdown factor (qemu/native): " << std::fixed << std::setprecision(3)
                << slowdown_factor << "x\n";
        std::cout << "  Throughput ratio (qemu/native): " << std::fixed << std::setprecision(3)
                << throughput_ratio_qemu_vs_native << "x\n";
        std::cout << "  Throughput change vs native: " << std::fixed << std::setprecision(2)
                << throughput_change_pct_qemu_vs_native << "%\n";
        std::cout << "  Commands match: " << (commands_match ? "yes" : "no") << "\n";
        std::cout << "  QEMU arch(es): "
                << (qemu_arch_list.empty() ? "unknown" : JoinCommaSeparated(qemu_arch_list)) << "\n";

        std::cout << "  Counter ratios (qemu/native, caveated):\n";
        if (counter_ratios.empty()) {
            std::cout << "    - unavailable\n";
        } else {
            for (const auto &kv: counter_ratios) {
                std::cout << "    - " << kv.first << ": " << std::fixed << std::setprecision(3) << kv.second
                        << "x\n";
            }
        }

        std::cout << "  Caveats:\n";
        for (const std::string &caveat: caveats) {
            std::cout << "    - " << caveat << "\n";
        }

        if (!json_path.empty()) {
            std::ostringstream json;
            json << "{\n"
                    << "  \"schema_version\": \"" << kSchemaVersion << "\",\n"
                    << "  \"kind\": \"compare_result\",\n"
                    << "  \"timestamp_utc\": \"" << NowUtcIso8601() << "\",\n"
                    << "  \"inputs\": {\n"
                    << "    \"native_files\": " << JsonStringArray(native_paths.empty()
                                                                       ? std::vector<std::string>{
                                                                           native_samples.front().path
                                                                       }
                                                                       : native_paths)
                    << ",\n"
                    << "    \"qemu_files\": " << JsonStringArray(qemu_paths.empty()
                                                                     ? std::vector<std::string>{
                                                                         qemu_samples.front().path
                                                                     }
                                                                     : qemu_paths)
                    << ",\n"
                    << "    \"commands_match\": " << (commands_match ? "true" : "false") << ",\n"
                    << "    \"command\": \"" << JsonEscape(baseline_command) << "\"\n"
                    << "  },\n"
                    << "  \"native\": {\n"
                    << "    \"sample_count\": " << native_samples.size() << ",\n"
                    << "    \"median_duration_sec\": " << std::fixed << std::setprecision(6) << native_median
                    << "\n"
                    << "  },\n"
                    << "  \"qemu\": {\n"
                    << "    \"sample_count\": " << qemu_samples.size() << ",\n"
                    << "    \"median_duration_sec\": " << std::fixed << std::setprecision(6) << qemu_median
                    << ",\n"
                    << "    \"arches\": " << JsonStringArray(qemu_arch_list) << "\n"
                    << "  },\n"
                    << "  \"comparison\": {\n"
                    << "    \"delta_duration_sec\": " << std::fixed << std::setprecision(6)
                    << delta_duration_sec << ",\n"
                    << "    \"slowdown_factor_qemu_vs_native\": " << std::fixed << std::setprecision(6)
                    << slowdown_factor << ",\n"
                    << "    \"throughput_ratio_qemu_vs_native\": " << std::fixed << std::setprecision(6)
                    << throughput_ratio_qemu_vs_native << ",\n"
                    << "    \"throughput_change_percent_qemu_vs_native\": " << std::fixed
                    << std::setprecision(6) << throughput_change_pct_qemu_vs_native << ",\n"
                    << "    \"perf_counter_ratio_qemu_vs_native\": " << JsonCounterRatioObject(counter_ratios)
                    << "\n"
                    << "  },\n"
                    << "  \"protocol\": {\n"
                    << "    \"recommended_warmup_runs\": 1,\n"
                    << "    \"recommended_measured_runs\": 5,\n"
                    << "    \"provided_native_samples\": " << native_samples.size() << ",\n"
                    << "    \"provided_qemu_samples\": " << qemu_samples.size() << ",\n"
                    << "    \"uses_recommended_sample_count\": "
                    << ((native_samples.size() == 5 && qemu_samples.size() == 5) ? "true" : "false") << "\n"
                    << "  },\n"
                    << "  \"caveats\": " << JsonStringArray(caveats) << "\n"
                    << "}\n";

            std::string error;
            if (!WriteTextFile(json_path, json.str(), &error)) {
                std::cerr << "compare: failed to write " << json_path << ": " << error << "\n";
                return 2;
            }
            std::cout << "  JSON: " << json_path << "\n";
        }

        return 0;
    }
} // namespace tracelab