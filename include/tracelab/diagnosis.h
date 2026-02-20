#pragma once

#include "tracelab/collectors.h"

#include <string>
#include <vector>

namespace tracelab {

// Single metric citation used to justify a diagnosis label.
struct DiagnosisEvidence {
    std::string metric;
    std::string value;
    std::string detail;
};

// Rule-engine output consumed by run JSON and report renderer.
struct DiagnosisResult {
    std::string label = "inconclusive";
    std::string confidence = "low";
    std::vector<DiagnosisEvidence> evidence;
    std::vector<std::string> limitations;
};

// Computes a bottleneck diagnosis from collected metrics.
DiagnosisResult DiagnoseRun(const WorkloadRunResult &workload, const PerfStatResult &perf,
                            const StraceSummaryResult &strace, const std::string &mode);

// Serializes a diagnosis object as JSON with the requested indentation.
std::string DiagnosisToJson(const DiagnosisResult &diagnosis, int indent);

} // namespace tracelab

