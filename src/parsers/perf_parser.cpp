#include "tracelab/parsers.h"
#include "tracelab/util.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace tracelab {

namespace {

// Normalizes locale/grouping variants before numeric parsing.
std::string CanonicalizeNumericText(std::string value) {
    value = Trim(value);

    // Remove spaces used as thousands separators.
    value.erase(std::remove(value.begin(), value.end(), ' '), value.end());

    const size_t comma_count = std::count(value.begin(), value.end(), ',');
    const size_t dot_count = std::count(value.begin(), value.end(), '.');

    if (comma_count > 0 && dot_count == 0) {
        // If it looks like a thousands separator pattern, drop commas.
        const size_t last_comma = value.rfind(',');
        const size_t digits_after = (last_comma == std::string::npos) ? 0 : (value.size() - last_comma - 1);
        if (comma_count >= 2 || digits_after == 3) {
            value.erase(std::remove(value.begin(), value.end(), ','), value.end());
        } else {
            std::replace(value.begin(), value.end(), ',', '.');
        }
    } else if (comma_count > 0 && dot_count > 0) {
        // Assume commas are grouping separators in mixed format.
        value.erase(std::remove(value.begin(), value.end(), ','), value.end());
    }

    return value;
}

// Splits perf CSV rows; some locales/tools emit ';' instead of ','.
std::vector<std::string> SplitCsv(const std::string &line) {
    const char delimiter = (line.find(';') != std::string::npos) ? ';' : ',';
    std::vector<std::string> parts;
    std::string current;
    for (char ch : line) {
        if (ch == delimiter) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

// Parses a possibly localized numeric token into double.
std::optional<double> ParseNumericCounter(const std::string &value) {
    const std::string canonical = CanonicalizeNumericText(value);
    std::string cleaned;
    for (char ch : canonical) {
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == 'e' ||
            ch == 'E') {
            cleaned.push_back(ch);
        }
    }
    if (cleaned.empty()) {
        return std::nullopt;
    }
    try {
        return std::stod(cleaned);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

// Parses `perf stat -x,` output and captures known counter names.
bool ParsePerfStatCsvOutput(const std::string &text, PerfStatData *data) {
    if (data == nullptr) {
        return false;
    }

    bool parsed_any = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::vector<std::string> fields = SplitCsv(line);
        if (fields.size() < 3) {
            continue;
        }

        const std::string maybe_value = Trim(fields[0]);
        const std::string event = Trim(fields[2]);
        const auto numeric_value = ParseNumericCounter(maybe_value);
        if (!numeric_value.has_value()) {
            continue;
        }
        const double v = numeric_value.value();

        if (event == "cycles") {
            data->cycles = v;
            data->has_cycles = true;
            parsed_any = true;
        } else if (event == "instructions") {
            data->instructions = v;
            data->has_instructions = true;
            parsed_any = true;
        } else if (event == "branches") {
            data->branches = v;
            data->has_branches = true;
            parsed_any = true;
        } else if (event == "branch-misses") {
            data->branch_misses = v;
            data->has_branch_misses = true;
            parsed_any = true;
        } else if (event == "cache-misses") {
            data->cache_misses = v;
            data->has_cache_misses = true;
            parsed_any = true;
        } else if (event == "page-faults") {
            data->page_faults = v;
            data->has_page_faults = true;
            parsed_any = true;
        }
    }

    return parsed_any;
}

} // namespace tracelab
