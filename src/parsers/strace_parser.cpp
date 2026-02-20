#include "tracelab/parsers.h"
#include "tracelab/util.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace tracelab {
    namespace {
        // Tokenizes a summary row by collapsing variable whitespace.
        std::vector<std::string> SplitWhitespace(const std::string &line) {
            std::istringstream in(line);
            std::vector<std::string> tokens;
            std::string token;
            while (in >> token) {
                tokens.push_back(token);
            }
            return tokens;
        }

        // Best-effort integer parse helper used for call/error counts.
        std::optional<long long> ParseLongLong(const std::string &value) {
            try {
                return std::stoll(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        // Parses float values, tolerating localized decimal/grouping separators.
        std::optional<double> ParseDouble(const std::string &value) {
            std::string normalized = Trim(value);
            normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '), normalized.end());

            const size_t comma_count = std::count(normalized.begin(), normalized.end(), ',');
            const size_t dot_count = std::count(normalized.begin(), normalized.end(), '.');
            if (comma_count > 0 && dot_count == 0) {
                std::replace(normalized.begin(), normalized.end(), ',', '.');
            } else if (comma_count > 0 && dot_count > 0) {
                normalized.erase(std::remove(normalized.begin(), normalized.end(), ','), normalized.end());
            }

            try {
                return std::stod(normalized);
            } catch (...) {
                return std::nullopt;
            }
        }
    } // namespace

    // Parses `strace -c` summary output into per-syscall rows and total time.
    bool ParseStraceSummaryOutput(const std::string &text, StraceSummaryData *data) {
        if (data == nullptr) {
            return false;
        }

        bool parsed_any = false;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || StartsWith(trimmed, "% time") || StartsWith(trimmed, "------")) {
                continue;
            }

            const std::vector<std::string> tokens = SplitWhitespace(trimmed);
            if (tokens.size() < 5) {
                continue;
            }

            const std::string syscall_name = tokens.back();
            const auto maybe_seconds = ParseDouble(tokens[1]);
            if (!maybe_seconds.has_value()) {
                continue;
            }

            if (syscall_name == "total") {
                data->total_time_sec = maybe_seconds.value();
                data->has_total_time = true;
                parsed_any = true;
                continue;
            }

            const auto maybe_calls = ParseLongLong(tokens[3]);
            if (!maybe_calls.has_value()) {
                continue;
            }

            long long errors = 0;
            if (tokens.size() >= 6) {
                const auto maybe_errors = ParseLongLong(tokens[4]);
                if (maybe_errors.has_value()) {
                    errors = maybe_errors.value();
                }
            }

            StraceSyscallEntry entry;
            entry.name = syscall_name;
            entry.calls = maybe_calls.value();
            entry.time_sec = maybe_seconds.value();
            entry.errors = errors;
            data->entries.push_back(entry);
            parsed_any = true;
        }

        return parsed_any;
    }
} // namespace tracelab