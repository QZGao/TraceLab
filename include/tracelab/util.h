#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tracelab {
    struct CommandResult {
        int exit_code = -1;
        std::string output;
    };

    // Common collector status used in run artifacts.
    // `status` is expected to be one of: ok, error, unavailable, pending_implementation.
    struct CollectorStatus {
        std::string status;
        std::string reason;
    };

    // Removes leading and trailing ASCII whitespace.
    std::string Trim(const std::string &value);

    // Returns true when `value` begins with `prefix`.
    bool StartsWith(const std::string &value, const std::string &prefix);

    // Lowercases ASCII letters.
    std::string ToLower(std::string value);

    // Escapes arbitrary text for safe JSON string emission.
    std::string JsonEscape(const std::string &value);

    // Produces a shell-safe quoted argument for the current platform shell.
    std::string ShellQuote(const std::string &value);

    // Joins argv-like parts as plain text (for display only).
    std::string JoinRaw(const std::vector<std::string> &parts);

    // Joins argv-like parts with shell quoting (for execution strings).
    std::string JoinQuoted(const std::vector<std::string> &parts);

    // Runs a shell command and returns only decoded exit code.
    int RunCommandStatus(const std::string &command);

    // Runs a shell command and captures stdout/stderr text plus decoded exit code.
    CommandResult RunCommandCapture(const std::string &command);

    // Returns true when an executable is available in PATH.
    bool CommandExists(const std::string &tool);

    // Writes text to a file, creating parent directories as needed.
    bool WriteTextFile(const std::string &path, const std::string &content, std::string *error);

    // Reads the entire text file into memory.
    std::optional<std::string> ReadTextFile(const std::string &path);

    // Returns true if the path exists and is readable as a file.
    bool FileExists(const std::string &path);

    // Returns current UTC timestamp in ISO-8601 format.
    std::string NowUtcIso8601();

    // Returns host OS identifier (for example: linux, windows, darwin).
    std::string HostOs();

    // Returns host architecture identifier (for example: x86_64, aarch64).
    std::string HostArch();

    // Returns kernel version string for reproducibility metadata.
    std::string KernelVersion();

    // Returns CPU model/brand string when available.
    std::string CpuModel();

    // Returns CPU governor hint (for example: performance/powersave) when available.
    std::string CpuGovernorHint();

    // Returns a tool version string (first --version line) or a sentinel such as missing/unknown.
    std::string ToolVersion(const std::string &tool);

    // Returns short git SHA for current repo, or "unknown" when unavailable.
    std::string DetectGitSha();

    // Extracts a string field by key from TraceLab JSON text.
    std::optional<std::string> ExtractJsonString(const std::string &text, const std::string &key);

    // Extracts a numeric field by key from TraceLab JSON text.
    std::optional<double> ExtractJsonNumber(const std::string &text, const std::string &key);

    // Extracts an integer field by key from TraceLab JSON text.
    std::optional<int> ExtractJsonInteger(const std::string &text, const std::string &key);

    // Extracts `collectors.<collector>.status` from TraceLab JSON text.
    std::optional<std::string> ExtractCollectorStatus(const std::string &text, const std::string &collector);

    // Extracts a "Label: value" field from plain-text command output.
    std::optional<std::string> ExtractLabeledField(const std::string &text, const std::string &label);

    // Shell null-device redirect fragment for the current platform.
    const char *NullRedirect();
} // namespace tracelab
