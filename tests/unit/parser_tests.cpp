#include "tracelab/parsers.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string &path) {
    std::ifstream in(path, std::ios::in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool NearlyEqual(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) <= eps;
}

int TestPerfSample() {
    const std::string text = ReadFile("tests/parser_fixtures/perf_stat_sample.csv");
    tracelab::PerfStatData data;
    const bool ok = tracelab::ParsePerfStatCsvOutput(text, &data);
    if (!ok) {
        std::cerr << "TestPerfSample: parser returned false\n";
        return 1;
    }
    if (!data.has_cycles || !NearlyEqual(data.cycles, 1000.0)) {
        std::cerr << "TestPerfSample: cycles mismatch\n";
        return 1;
    }
    if (!data.has_instructions || !NearlyEqual(data.instructions, 2500.0)) {
        std::cerr << "TestPerfSample: instructions mismatch\n";
        return 1;
    }
    if (!data.has_page_faults || !NearlyEqual(data.page_faults, 12.0)) {
        std::cerr << "TestPerfSample: page_faults mismatch\n";
        return 1;
    }
    return 0;
}

int TestPerfUnsupported() {
    const std::string text = ReadFile("tests/parser_fixtures/perf_stat_with_unsupported.csv");
    tracelab::PerfStatData data;
    const bool ok = tracelab::ParsePerfStatCsvOutput(text, &data);
    if (!ok) {
        std::cerr << "TestPerfUnsupported: parser returned false\n";
        return 1;
    }
    if (data.has_cycles) {
        std::cerr << "TestPerfUnsupported: unsupported cycles should not parse\n";
        return 1;
    }
    if (!data.has_instructions || !NearlyEqual(data.instructions, 200.0)) {
        std::cerr << "TestPerfUnsupported: instructions mismatch\n";
        return 1;
    }
    return 0;
}

int TestPerfLocalizedSemicolon() {
    const std::string text = ReadFile("tests/parser_fixtures/perf_stat_localized_semicolon.csv");
    tracelab::PerfStatData data;
    const bool ok = tracelab::ParsePerfStatCsvOutput(text, &data);
    if (!ok) {
        std::cerr << "TestPerfLocalizedSemicolon: parser returned false\n";
        return 1;
    }
    if (!data.has_cycles || !NearlyEqual(data.cycles, 1234.0)) {
        std::cerr << "TestPerfLocalizedSemicolon: cycles mismatch\n";
        return 1;
    }
    if (!data.has_instructions || !NearlyEqual(data.instructions, 2468.0)) {
        std::cerr << "TestPerfLocalizedSemicolon: instructions mismatch\n";
        return 1;
    }
    return 0;
}

int TestPerfMissingFields() {
    const std::string text = ReadFile("tests/parser_fixtures/perf_stat_missing_fields.csv");
    tracelab::PerfStatData data;
    const bool ok = tracelab::ParsePerfStatCsvOutput(text, &data);
    if (!ok) {
        std::cerr << "TestPerfMissingFields: parser returned false\n";
        return 1;
    }
    if (!data.has_cycles || !NearlyEqual(data.cycles, 555.0)) {
        std::cerr << "TestPerfMissingFields: cycles mismatch\n";
        return 1;
    }
    return 0;
}

int TestStraceSample() {
    const std::string text = ReadFile("tests/parser_fixtures/strace_summary_sample.txt");
    tracelab::StraceSummaryData data;
    const bool ok = tracelab::ParseStraceSummaryOutput(text, &data);
    if (!ok) {
        std::cerr << "TestStraceSample: parser returned false\n";
        return 1;
    }
    if (data.entries.size() != 2) {
        std::cerr << "TestStraceSample: expected 2 syscall entries\n";
        return 1;
    }
    if (data.entries[0].name != "futex" || data.entries[0].calls != 300 ||
        !NearlyEqual(data.entries[0].time_sec, 0.03) || data.entries[0].errors != 4) {
        std::cerr << "TestStraceSample: futex row mismatch\n";
        return 1;
    }
    if (data.entries[1].name != "read" || data.entries[1].calls != 1000 ||
        data.entries[1].errors != 0) {
        std::cerr << "TestStraceSample: read row mismatch\n";
        return 1;
    }
    if (!data.has_total_time || !NearlyEqual(data.total_time_sec, 0.04)) {
        std::cerr << "TestStraceSample: total row mismatch\n";
        return 1;
    }
    return 0;
}

int TestStraceLocalizedNonZero() {
    const std::string text = ReadFile("tests/parser_fixtures/strace_summary_localized_nonzero.txt");
    tracelab::StraceSummaryData data;
    const bool ok = tracelab::ParseStraceSummaryOutput(text, &data);
    if (!ok) {
        std::cerr << "TestStraceLocalizedNonZero: parser returned false\n";
        return 1;
    }
    if (data.entries.size() != 2) {
        std::cerr << "TestStraceLocalizedNonZero: expected 2 syscall entries\n";
        return 1;
    }
    if (data.entries[0].name != "read" || !NearlyEqual(data.entries[0].time_sec, 0.008)) {
        std::cerr << "TestStraceLocalizedNonZero: read row mismatch\n";
        return 1;
    }
    if (data.entries[1].name != "write" || data.entries[1].errors != 1) {
        std::cerr << "TestStraceLocalizedNonZero: write row mismatch\n";
        return 1;
    }
    if (!data.has_total_time || !NearlyEqual(data.total_time_sec, 0.01)) {
        std::cerr << "TestStraceLocalizedNonZero: total row mismatch\n";
        return 1;
    }
    return 0;
}

int TestStraceSparse() {
    const std::string text = ReadFile("tests/parser_fixtures/strace_summary_sparse.txt");
    tracelab::StraceSummaryData data;
    const bool ok = tracelab::ParseStraceSummaryOutput(text, &data);
    if (!ok) {
        std::cerr << "TestStraceSparse: parser returned false\n";
        return 1;
    }
    if (data.entries.size() != 2) {
        std::cerr << "TestStraceSparse: expected 2 syscall entries\n";
        return 1;
    }
    if (data.entries[0].name != "read" || data.entries[0].errors != 0) {
        std::cerr << "TestStraceSparse: read row mismatch\n";
        return 1;
    }
    if (data.entries[1].name != "write" || data.entries[1].errors != 0) {
        std::cerr << "TestStraceSparse: write row mismatch\n";
        return 1;
    }
    if (!data.has_total_time || !NearlyEqual(data.total_time_sec, 0.01)) {
        std::cerr << "TestStraceSparse: total row mismatch\n";
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failed = 0;
    failed += TestPerfSample();
    failed += TestPerfUnsupported();
    failed += TestPerfLocalizedSemicolon();
    failed += TestPerfMissingFields();
    failed += TestStraceSample();
    failed += TestStraceLocalizedNonZero();
    failed += TestStraceSparse();

    if (failed == 0) {
        std::cout << "All parser tests passed\n";
        return 0;
    }
    std::cerr << failed << " parser tests failed\n";
    return 1;
}
