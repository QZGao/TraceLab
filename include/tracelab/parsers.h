#pragma once

#include "tracelab/collectors.h"

#include <string>

namespace tracelab {
    // Parses CSV-like `perf stat -x` output into `PerfStatData`.
    // Returns true when at least one supported counter is extracted.
    bool ParsePerfStatCsvOutput(const std::string &text, PerfStatData *data);

    // Parses `strace -c` summary output into `StraceSummaryData`.
    // Returns true when at least one syscall row or total row is extracted.
    bool ParseStraceSummaryOutput(const std::string &text, StraceSummaryData *data);
} // namespace tracelab