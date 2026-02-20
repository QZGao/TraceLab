#include "tracelab/collectors.h"
#include "tracelab/util.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tracelab {
    namespace {
        // Parses an integer prefix from lines like "123 kB".
        std::optional<long long> ParseLeadingInteger(const std::string &value) {
            std::istringstream in(value);
            long long parsed = 0;
            if (in >> parsed) {
                return parsed;
            }
            return std::nullopt;
        }

        // Extracts RSS/context-switch fields from /proc/<pid>/status text.
        void UpdateProcSampleFromStatusText(const std::string &status_text, ProcStatusSample *sample) {
            std::istringstream in(status_text);
            std::string line;
            while (std::getline(in, line)) {
                const std::string trimmed = Trim(line);
                if (StartsWith(trimmed, "VmRSS:")) {
                    const auto maybe_kb = ParseLeadingInteger(Trim(trimmed.substr(6)));
                    if (maybe_kb.has_value()) {
                        const long long kb = maybe_kb.value();
                        if (!sample->has_max_rss_kb || kb > sample->max_rss_kb) {
                            sample->max_rss_kb = kb;
                            sample->has_max_rss_kb = true;
                        }
                    }
                } else if (StartsWith(trimmed, "voluntary_ctxt_switches:")) {
                    const auto maybe_count = ParseLeadingInteger(Trim(trimmed.substr(24)));
                    if (maybe_count.has_value()) {
                        sample->voluntary_ctxt_switches = maybe_count.value();
                        sample->has_voluntary_ctxt_switches = true;
                    }
                } else if (StartsWith(trimmed, "nonvoluntary_ctxt_switches:")) {
                    const auto maybe_count = ParseLeadingInteger(Trim(trimmed.substr(27)));
                    if (maybe_count.has_value()) {
                        sample->nonvoluntary_ctxt_switches = maybe_count.value();
                        sample->has_nonvoluntary_ctxt_switches = true;
                    }
                }
            }
        }

#ifdef __linux__
        // Converts waitpid status to a shell-like exit code and classification string.
        int DecodeWaitStatus(int status, std::string *classification) {
            if (WIFEXITED(status)) {
                if (classification != nullptr) {
                    *classification = "exit_code";
                }
                return WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status)) {
                if (classification != nullptr) {
                    *classification = "signal";
                }
                return 128 + WTERMSIG(status);
            }
            if (classification != nullptr) {
                *classification = "unknown";
            }
            return status;
        }
#endif
    } // namespace

    // Runs a workload and samples /proc status while the process is alive.
    WorkloadRunResult RunWithProcSampling(const std::vector<std::string> &command) {
        WorkloadRunResult result;
        if (command.empty()) {
            result.exit_code = 2;
            result.exit_classification = "argument_error";
            result.proc_collector_status = {"error", "empty command"};
            return result;
        }

        const auto start = std::chrono::steady_clock::now();

#ifdef __linux__
        // Fork/exec lets us sample `/proc/<pid>/status` while workload is alive.
        pid_t pid = fork();
        if (pid < 0) {
            result.exit_code = 2;
            result.exit_classification = "spawn_error";
            result.proc_collector_status = {"error", "fork failed"};
            return result;
        }

        if (pid == 0) {
            std::vector<char *> argv;
            argv.reserve(command.size() + 1);
            for (const std::string &arg: command) {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }

        // Poll /proc/<pid>/status until child exits to collect fallback metrics.
        bool saw_proc_status = false;
        int wait_status = 0;
        while (true) {
            const std::string status_path = "/proc/" + std::to_string(pid) + "/status";
            const auto maybe_status_text = ReadTextFile(status_path);
            if (maybe_status_text.has_value()) {
                UpdateProcSampleFromStatusText(maybe_status_text.value(), &result.proc_sample);
                saw_proc_status = true;
            }

            const pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
            if (wait_result == pid) {
                break;
            }
            if (wait_result < 0) {
                result.exit_code = 2;
                result.exit_classification = "wait_error";
                result.proc_collector_status = {"error", "waitpid failed"};
                const auto end = std::chrono::steady_clock::now();
                result.wall_time_sec =
                        std::chrono::duration<double>(end - start).count();
                return result;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        const std::string final_status_path = "/proc/" + std::to_string(pid) + "/status";
        const auto maybe_final_status_text = ReadTextFile(final_status_path);
        if (maybe_final_status_text.has_value()) {
            UpdateProcSampleFromStatusText(maybe_final_status_text.value(), &result.proc_sample);
            saw_proc_status = true;
        }

        result.exit_code = DecodeWaitStatus(wait_status, &result.exit_classification);
        result.proc_collector_status =
                saw_proc_status
                    ? CollectorStatus{"ok", ""}
                    : CollectorStatus{"unavailable", "unable to read /proc/<pid>/status"};
#else
        result.exit_code = RunCommandStatus(JoinQuoted(command));
        result.exit_classification = "exit_code";
        result.proc_collector_status = {"unavailable", "/proc collector is Linux-only"};
#endif

        const auto end = std::chrono::steady_clock::now();
        result.wall_time_sec = std::chrono::duration<double>(end - start).count();
        return result;
    }
} // namespace tracelab