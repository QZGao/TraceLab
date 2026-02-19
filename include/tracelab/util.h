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

// String helpers.
std::string Trim(const std::string &value);
bool StartsWith(const std::string &value, const std::string &prefix);
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

// File helpers.
bool WriteTextFile(const std::string &path, const std::string &content, std::string *error);
std::optional<std::string> ReadTextFile(const std::string &path);
bool FileExists(const std::string &path);

// Platform and metadata helpers.
std::string NowUtcIso8601();
std::string HostOs();
std::string HostArch();
std::string DetectGitSha();

// Lightweight JSON extraction helpers used by `report`.
// These are intentionally simple and operate on known TraceLab JSON layout.
std::optional<std::string> ExtractJsonString(const std::string &text, const std::string &key);
std::optional<double> ExtractJsonNumber(const std::string &text, const std::string &key);
std::optional<int> ExtractJsonInteger(const std::string &text, const std::string &key);
std::optional<std::string> ExtractCollectorStatus(const std::string &text, const std::string &collector);
std::optional<std::string> ExtractLabeledField(const std::string &text, const std::string &label);

// Shell null-device redirect fragment for the current platform.
const char *NullRedirect();

} // namespace tracelab
