#include "tracelab/util.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace tracelab {
    // Trims leading/trailing ASCII whitespace.
    std::string Trim(const std::string &value) {
        const std::string whitespace = " \t\r\n";
        const size_t begin = value.find_first_not_of(whitespace);
        if (begin == std::string::npos) {
            return "";
        }
        const size_t end = value.find_last_not_of(whitespace);
        return value.substr(begin, end - begin + 1);
    }

    // Checks whether `value` starts with `prefix`.
    bool StartsWith(const std::string &value, const std::string &prefix) {
        return value.rfind(prefix, 0) == 0;
    }

    // Lowercases ASCII characters without locale dependencies.
    std::string ToLower(std::string value) {
        for (char &ch: value) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        return value;
    }

    // Escapes raw text for safe insertion into JSON string values.
    std::string JsonEscape(const std::string &value) {
        std::ostringstream out;
        for (unsigned char ch: value) {
            switch (ch) {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\b':
                    out << "\\b";
                    break;
                case '\f':
                    out << "\\f";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    if (ch < 0x20) {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                << static_cast<int>(ch) << std::dec;
                    } else {
                        out << static_cast<char>(ch);
                    }
                    break;
            }
        }
        return out.str();
    }

    // Quotes a shell argument for the host platform shell.
    std::string ShellQuote(const std::string &value) {
#ifdef _WIN32
        std::string escaped = "\"";
        for (char ch: value) {
            if (ch == '"') {
                escaped += "\\\"";
            } else {
                escaped += ch;
            }
        }
        escaped += "\"";
        return escaped;
#else
        if (value.empty()) {
            return "''";
        }
        std::string escaped = "'";
        for (char ch: value) {
            if (ch == '\'') {
                escaped += "'\\''";
            } else {
                escaped += ch;
            }
        }
        escaped += "'";
        return escaped;
#endif
    }

    // Joins arguments verbatim for display.
    std::string JoinRaw(const std::vector<std::string> &parts) {
        std::ostringstream out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ' ';
            }
            out << parts[i];
        }
        return out.str();
    }

    // Joins arguments with shell quoting for command execution strings.
    std::string JoinQuoted(const std::vector<std::string> &parts) {
        std::ostringstream out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ' ';
            }
            out << ShellQuote(parts[i]);
        }
        return out.str();
    }

    // Normalizes platform-specific process status codes.
    static int DecodeProcessStatus(int status) {
#ifdef _WIN32
        return status;
#else
        if (status == -1) {
            return -1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return status;
#endif
    }

    // Runs a shell command and returns decoded exit status only.
    int RunCommandStatus(const std::string &command) {
        const int status = std::system(command.c_str());
        return DecodeProcessStatus(status);
    }

    // Runs a shell command and captures combined stdout/stderr text.
    CommandResult RunCommandCapture(const std::string &command) {
#ifdef _WIN32
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif
        if (pipe == nullptr) {
            return {-1, ""};
        }

        std::string output;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }

#ifdef _WIN32
        const int status = _pclose(pipe);
#else
        const int status = pclose(pipe);
#endif

        return {DecodeProcessStatus(status), output};
    }

    // Checks whether an executable can be resolved from PATH.
    bool CommandExists(const std::string &tool) {
#ifdef _WIN32
        const std::string command = "where " + ShellQuote(tool) + NullRedirect();
#else
        const std::string command = "command -v " + ShellQuote(tool) + NullRedirect();
#endif
        return RunCommandStatus(command) == 0;
    }

    // Writes a text file, creating parent directories when needed.
    bool WriteTextFile(const std::string &path, const std::string &content, std::string *error) {
        const std::filesystem::path output_path(path);
        if (output_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(output_path.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = "unable to create parent directory";
                }
                return false;
            }
        }

        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            if (error != nullptr) {
                *error = "unable to open output file";
            }
            return false;
        }
        out << content;
        if (!out.good()) {
            if (error != nullptr) {
                *error = "failed while writing output file";
            }
            return false;
        }
        return true;
    }

    // Reads an entire text file into memory.
    std::optional<std::string> ReadTextFile(const std::string &path) {
        std::ifstream in(path, std::ios::in);
        if (!in) {
            return std::nullopt;
        }
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    // Lightweight file existence check used by CLI argument validation.
    bool FileExists(const std::string &path) {
        std::ifstream in(path, std::ios::in);
        return static_cast<bool>(in);
    }

    // Returns current UTC time in ISO-8601 format used by JSON artifacts.
    std::string NowUtcIso8601() {
        using std::chrono::system_clock;
        const auto now = system_clock::now();
        const std::time_t now_time = system_clock::to_time_t(now);
        std::tm utc_time{};
#ifdef _WIN32
        gmtime_s(&utc_time, &now_time);
#else
        gmtime_r(&now_time, &utc_time);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
        return buffer;
    }

    // Best-effort host OS identifier for artifact metadata.
    std::string HostOs() {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "darwin";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown";
#endif
    }

    // Best-effort host architecture identifier for artifact metadata.
    std::string HostArch() {
#if defined(_M_X64) || defined(__x86_64__)
        return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
        return "aarch64";
#elif defined(__riscv) && (__riscv_xlen == 64)
        return "riscv64";
#elif defined(_M_IX86) || defined(__i386__)
        return "x86";
#else
        return "unknown";
#endif
    }

    // Returns kernel release identifier for reproducibility metadata.
    std::string KernelVersion() {
#if defined(__linux__) || defined(__APPLE__)
        const CommandResult result = RunCommandCapture("uname -r 2>/dev/null");
        const std::string kernel = Trim(result.output);
        if (result.exit_code == 0 && !kernel.empty()) {
            return kernel;
        }
#endif
        return "unknown";
    }

    // Returns CPU model/brand string when host metadata is readable.
    std::string CpuModel() {
#if defined(__linux__)
        const auto cpuinfo = ReadTextFile("/proc/cpuinfo");
        if (cpuinfo.has_value()) {
            std::istringstream in(cpuinfo.value());
            std::string line;
            while (std::getline(in, line)) {
                const size_t separator = line.find(':');
                if (separator == std::string::npos) {
                    continue;
                }

                const std::string key = ToLower(Trim(line.substr(0, separator)));
                if (key == "model name" || key == "hardware") {
                    const std::string value = Trim(line.substr(separator + 1));
                    if (!value.empty()) {
                        return value;
                    }
                }
            }
        }
#elif defined(__APPLE__)
        const CommandResult result = RunCommandCapture("sysctl -n machdep.cpu.brand_string 2>/dev/null");
        const std::string model = Trim(result.output);
        if (result.exit_code == 0 && !model.empty()) {
            return model;
        }
#endif
        return "unknown";
    }

    // Returns a best-effort CPU frequency governor hint for Linux hosts.
    std::string CpuGovernorHint() {
#if defined(__linux__)
        const auto governor = ReadTextFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        if (governor.has_value()) {
            const std::string value = Trim(governor.value());
            if (!value.empty()) {
                return value;
            }
            return "unknown";
        }
        return "unavailable";
#else
        return "unavailable";
#endif
    }

    // Resolves a tool's version string from the first non-empty `--version` output line.
    std::string ToolVersion(const std::string &tool) {
        if (!CommandExists(tool)) {
            return "missing";
        }

        const std::string command = ShellQuote(tool) + " --version 2>&1";
        const CommandResult result = RunCommandCapture(command);
        std::istringstream in(result.output);
        std::string line;
        while (std::getline(in, line)) {
            const std::string trimmed = Trim(line);
            if (!trimmed.empty()) {
                return trimmed;
            }
        }

        return result.exit_code == 0 ? "unknown" : "error";
    }

    // Returns short git SHA when repository/git is available.
    std::string DetectGitSha() {
#ifdef _WIN32
        const std::string command = "git rev-parse --short HEAD 2>NUL";
#else
        const std::string command = "git rev-parse --short HEAD 2>/dev/null";
#endif
        const CommandResult result = RunCommandCapture(command);
        const std::string sha = Trim(result.output);
        if (result.exit_code == 0 && !sha.empty()) {
            return sha;
        }
        return "unknown";
    }

    // Extracts a top-level JSON string field with a simple regex.
    std::optional<std::string> ExtractJsonString(const std::string &text, const std::string &key) {
        const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch match;
        if (std::regex_search(text, match, pattern) && match.size() > 1) {
            return match[1].str();
        }
        return std::nullopt;
    }

    // Extracts a top-level JSON numeric field with a simple regex.
    std::optional<double> ExtractJsonNumber(const std::string &text, const std::string &key) {
        const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
        std::smatch match;
        if (std::regex_search(text, match, pattern) && match.size() > 1) {
            try {
                return std::stod(match[1].str());
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Extracts a top-level JSON integer field with a simple regex.
    std::optional<int> ExtractJsonInteger(const std::string &text, const std::string &key) {
        const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
        std::smatch match;
        if (std::regex_search(text, match, pattern) && match.size() > 1) {
            try {
                return std::stoi(match[1].str());
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Extracts `collectors.<collector>.status` from TraceLab run JSON text.
    std::optional<std::string> ExtractCollectorStatus(const std::string &text,
                                                      const std::string &collector) {
        const std::regex pattern("\"" + collector +
                                 "\"\\s*:\\s*\\{[\\s\\S]*?\"status\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(text, match, pattern) && match.size() > 1) {
            return match[1].str();
        }
        return std::nullopt;
    }

    // Extracts "Label: value" style text fields from command output.
    std::optional<std::string> ExtractLabeledField(const std::string &text, const std::string &label) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::string trimmed = Trim(line);
            if (StartsWith(trimmed, label)) {
                return Trim(trimmed.substr(label.size()));
            }
        }
        return std::nullopt;
    }

    // Platform-specific redirect fragment to suppress command output.
    const char *NullRedirect() {
#ifdef _WIN32
        return " >NUL 2>&1";
#else
        return " >/dev/null 2>&1";
#endif
    }
} // namespace tracelab
