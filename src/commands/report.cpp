#include "tracelab/commands.h"
#include "tracelab/util.h"

#include <iomanip>
#include <iostream>
#include <regex>
#include <vector>

namespace tracelab {
    namespace {
        // Collects diagnosis evidence triplets emitted in run-result JSON.
        std::vector<std::string> ExtractDiagnosisEvidenceLines(const std::string &json_text) {
            std::vector<std::string> lines;
            const std::regex pattern(
                "\"metric\"\\s*:\\s*\"([^\"]*)\"\\s*,\\s*\"value\"\\s*:\\s*\"([^\"]*)\"\\s*,\\s*\"detail\""
                "\\s*:\\s*\"([^\"]*)\"");
            for (std::sregex_iterator it(json_text.begin(), json_text.end(), pattern), end; it != end; ++it) {
                const std::smatch &m = *it;
                if (m.size() >= 4) {
                    lines.push_back(m[1].str() + ": " + m[2].str() + " (" + m[3].str() + ")");
                }
            }
            return lines;
        }

        // Collects quoted strings from `diagnosis.limitations`.
        std::vector<std::string> ExtractDiagnosisLimitations(const std::string &json_text) {
            std::vector<std::string> limitations;
            const std::regex block_pattern("\"limitations\"\\s*:\\s*\\[([^\\]]*)\\]");
            std::smatch block_match;
            if (!std::regex_search(json_text, block_match, block_pattern) || block_match.size() < 2) {
                return limitations;
            }

            const std::string block = block_match[1].str();
            const std::regex item_pattern("\"([^\"]*)\"");
            for (std::sregex_iterator it(block.begin(), block.end(), item_pattern), end; it != end; ++it) {
                const std::smatch &m = *it;
                if (m.size() >= 2) {
                    limitations.push_back(m[1].str());
                }
            }
            return limitations;
        }
    } // namespace

    // Implements `tracelab report`: renders a concise summary from a run-result JSON file.
    int HandleReport(const std::vector<std::string> &args) {
        if (args.empty() || args[0] == "--help") {
            std::cout << "Usage: tracelab report <result.json>\n";
            return args.empty() ? 2 : 0;
        }
        if (args.size() != 1) {
            std::cerr << "report: expected exactly one argument\n";
            return 2;
        }

        const std::string path = args[0];
        const auto content = ReadTextFile(path);
        if (!content.has_value()) {
            std::cerr << "report: failed to read " << path << "\n";
            return 2;
        }

        const auto kind = ExtractJsonString(content.value(), "kind");
        if (!kind.has_value() || kind.value() != "run_result") {
            std::cerr << "report: unsupported or missing kind field in " << path << "\n";
            return 2;
        }

        const std::string mode = ExtractJsonString(content.value(), "mode").value_or("unknown");
        const std::string command = ExtractJsonString(content.value(), "command").value_or("unknown");
        const auto duration = ExtractJsonNumber(content.value(), "duration_sec");
        const auto exit_code = ExtractJsonInteger(content.value(), "exit_code");
        const std::string perf =
                ExtractCollectorStatus(content.value(), "perf_stat").value_or("unknown");
        const std::string strace =
                ExtractCollectorStatus(content.value(), "strace_summary").value_or("unknown");
        const std::string proc =
                ExtractCollectorStatus(content.value(), "proc_status").value_or("unknown");
        const std::string diagnosis_label =
                ExtractJsonString(content.value(), "label").value_or("inconclusive");
        const std::string diagnosis_confidence =
                ExtractJsonString(content.value(), "confidence").value_or("unknown");
        const std::vector<std::string> evidence_lines = ExtractDiagnosisEvidenceLines(content.value());
        const std::vector<std::string> limitations = ExtractDiagnosisLimitations(content.value());

        std::cout << "TraceLab Report\n";
        std::cout << "  Source: " << path << "\n";
        std::cout << "  Mode: " << mode << "\n";
        std::cout << "  Command: " << command << "\n";
        if (duration.has_value()) {
            std::cout << "  Duration: " << std::fixed << std::setprecision(6) << duration.value()
                    << "s\n";
        } else {
            std::cout << "  Duration: unknown\n";
        }
        if (exit_code.has_value()) {
            std::cout << "  Exit code: " << exit_code.value() << "\n";
        } else {
            std::cout << "  Exit code: unknown\n";
        }
        std::cout << "  Collectors: perf_stat=" << perf << ", strace_summary=" << strace
                << ", proc_status=" << proc << "\n";
        std::cout << "  Diagnosis: " << diagnosis_label << "\n";
        std::cout << "  Confidence: " << diagnosis_confidence << "\n";
        std::cout << "  Evidence:\n";
        if (evidence_lines.empty()) {
            std::cout << "    - unavailable\n";
        } else {
            for (const std::string &line: evidence_lines) {
                std::cout << "    - " << line << "\n";
            }
        }
        std::cout << "  Limitations:\n";
        if (limitations.empty()) {
            std::cout << "    - none captured\n";
        } else {
            for (const std::string &line: limitations) {
                std::cout << "    - " << line << "\n";
            }
        }

        return 0;
    }
} // namespace tracelab