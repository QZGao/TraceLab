#include "tracelab/diagnosis.h"

#include <iostream>
#include <string>

namespace {

// Creates a baseline successful workload sample for synthetic diagnosis tests.
tracelab::WorkloadRunResult MakeBaseWorkload(double wall_time_sec) {
    tracelab::WorkloadRunResult workload;
    workload.exit_code = 0;
    workload.exit_classification = "exit_code";
    workload.wall_time_sec = wall_time_sec;
    workload.proc_collector_status = {"ok", ""};
    return workload;
}

// Creates a baseline perf result marked as usable.
tracelab::PerfStatResult MakeBasePerf() {
    tracelab::PerfStatResult perf;
    perf.status = {"ok", ""};
    return perf;
}

// Creates a baseline strace result marked as usable.
tracelab::StraceSummaryResult MakeBaseStrace() {
    tracelab::StraceSummaryResult strace;
    strace.status = {"ok", ""};
    strace.data.has_total_time = true;
    return strace;
}

// Asserts that a diagnosis has at least two evidence lines per v1 DoD.
int AssertHasAtLeastTwoEvidence(const tracelab::DiagnosisResult &diagnosis,
                                const std::string &test_name) {
    if (diagnosis.evidence.size() < 2) {
        std::cerr << test_name << ": expected at least two evidence entries\n";
        return 1;
    }
    return 0;
}

// Verifies CPU-bound classification using high IPC and low syscall share.
int TestCpuBoundDiagnosis() {
    tracelab::WorkloadRunResult workload = MakeBaseWorkload(1.0);

    tracelab::PerfStatResult perf = MakeBasePerf();
    perf.data.has_cycles = true;
    perf.data.cycles = 1'000'000'000.0;
    perf.data.has_instructions = true;
    perf.data.instructions = 1'500'000'000.0;
    perf.data.has_cache_misses = true;
    perf.data.cache_misses = 6'000'000.0;

    tracelab::StraceSummaryResult strace = MakeBaseStrace();
    strace.data.total_time_sec = 0.02;
    strace.data.entries.push_back({"futex", 30, 0.010, 0});
    strace.data.entries.push_back({"read", 10, 0.005, 0});

    const tracelab::DiagnosisResult diagnosis =
        tracelab::DiagnoseRun(workload, perf, strace, "native");
    if (diagnosis.label != "cpu-bound") {
        std::cerr << "TestCpuBoundDiagnosis: expected cpu-bound label\n";
        return 1;
    }
    return AssertHasAtLeastTwoEvidence(diagnosis, "TestCpuBoundDiagnosis");
}

// Verifies syscall-heavy classification when syscall share is dominant.
int TestSyscallHeavyDiagnosis() {
    tracelab::WorkloadRunResult workload = MakeBaseWorkload(1.0);

    tracelab::PerfStatResult perf = MakeBasePerf();
    perf.status = {"unavailable", "perf not found"};

    tracelab::StraceSummaryResult strace = MakeBaseStrace();
    strace.data.total_time_sec = 0.55;
    strace.data.entries.push_back({"futex", 500, 0.35, 0});
    strace.data.entries.push_back({"epoll_wait", 120, 0.15, 0});
    strace.data.entries.push_back({"read", 100, 0.05, 0});

    const tracelab::DiagnosisResult diagnosis =
        tracelab::DiagnoseRun(workload, perf, strace, "native");
    if (diagnosis.label != "syscall-heavy") {
        std::cerr << "TestSyscallHeavyDiagnosis: expected syscall-heavy label\n";
        return 1;
    }
    return AssertHasAtLeastTwoEvidence(diagnosis, "TestSyscallHeavyDiagnosis");
}

// Verifies I/O-bound classification when I/O syscalls dominate syscall time.
int TestIoBoundDiagnosis() {
    tracelab::WorkloadRunResult workload = MakeBaseWorkload(1.0);

    tracelab::PerfStatResult perf = MakeBasePerf();
    perf.status = {"unavailable", "perf not found"};

    tracelab::StraceSummaryResult strace = MakeBaseStrace();
    strace.data.total_time_sec = 0.40;
    strace.data.entries.push_back({"read", 800, 0.18, 0});
    strace.data.entries.push_back({"openat", 200, 0.08, 0});
    strace.data.entries.push_back({"fstat", 160, 0.05, 0});
    strace.data.entries.push_back({"futex", 40, 0.02, 0});

    const tracelab::DiagnosisResult diagnosis =
        tracelab::DiagnoseRun(workload, perf, strace, "native");
    if (diagnosis.label != "io-bound") {
        std::cerr << "TestIoBoundDiagnosis: expected io-bound label\n";
        return 1;
    }
    return AssertHasAtLeastTwoEvidence(diagnosis, "TestIoBoundDiagnosis");
}

// Verifies memory-pressure classification from RSS and fault/switch rates.
int TestMemoryPressureDiagnosis() {
    tracelab::WorkloadRunResult workload = MakeBaseWorkload(1.0);
    workload.proc_sample.has_max_rss_kb = true;
    workload.proc_sample.max_rss_kb = 900 * 1024;
    workload.proc_sample.has_voluntary_ctxt_switches = true;
    workload.proc_sample.voluntary_ctxt_switches = 8000;

    tracelab::PerfStatResult perf = MakeBasePerf();
    perf.data.has_page_faults = true;
    perf.data.page_faults = 3000.0;

    tracelab::StraceSummaryResult strace = MakeBaseStrace();
    strace.data.total_time_sec = 0.08;
    strace.data.entries.push_back({"read", 100, 0.03, 0});

    const tracelab::DiagnosisResult diagnosis =
        tracelab::DiagnoseRun(workload, perf, strace, "native");
    if (diagnosis.label != "memory-pressure") {
        std::cerr << "TestMemoryPressureDiagnosis: expected memory-pressure label\n";
        return 1;
    }
    return AssertHasAtLeastTwoEvidence(diagnosis, "TestMemoryPressureDiagnosis");
}

// Verifies QEMU runs include emulation caveats in limitations.
int TestQemuLimitations() {
    tracelab::WorkloadRunResult workload = MakeBaseWorkload(0.20);

    tracelab::PerfStatResult perf = MakeBasePerf();
    perf.data.has_cycles = true;
    perf.data.cycles = 2000.0;
    perf.data.has_instructions = true;
    perf.data.instructions = 2000.0;

    tracelab::StraceSummaryResult strace = MakeBaseStrace();
    strace.data.total_time_sec = 0.01;
    strace.data.entries.push_back({"read", 2, 0.005, 0});

    const tracelab::DiagnosisResult diagnosis =
        tracelab::DiagnoseRun(workload, perf, strace, "qemu");
    bool saw_qemu_limitation = false;
    for (const std::string &line : diagnosis.limitations) {
        if (line.find("QEMU emulation") != std::string::npos) {
            saw_qemu_limitation = true;
            break;
        }
    }
    if (!saw_qemu_limitation) {
        std::cerr << "TestQemuLimitations: expected QEMU limitation\n";
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failed = 0;
    failed += TestCpuBoundDiagnosis();
    failed += TestSyscallHeavyDiagnosis();
    failed += TestIoBoundDiagnosis();
    failed += TestMemoryPressureDiagnosis();
    failed += TestQemuLimitations();

    if (failed == 0) {
        std::cout << "All diagnosis rule tests passed\n";
        return 0;
    }
    std::cerr << failed << " diagnosis rule tests failed\n";
    return 1;
}

