#include "tracelab/qemu.h"

#include <iostream>

namespace {

int TestNormalizeAliases() {
    const auto x86 = tracelab::NormalizeQemuArchSelector("amd64");
    const auto arm = tracelab::NormalizeQemuArchSelector("arm64");
    const auto rv = tracelab::NormalizeQemuArchSelector("rv64");
    if (!x86.has_value() || x86.value() != "x86_64") {
        std::cerr << "TestNormalizeAliases: amd64 should normalize to x86_64\n";
        return 1;
    }
    if (!arm.has_value() || arm.value() != "aarch64") {
        std::cerr << "TestNormalizeAliases: arm64 should normalize to aarch64\n";
        return 1;
    }
    if (!rv.has_value() || rv.value() != "riscv64") {
        std::cerr << "TestNormalizeAliases: rv64 should normalize to riscv64\n";
        return 1;
    }
    return 0;
}

int TestUnsupportedSelector() {
    const auto unsupported = tracelab::NormalizeQemuArchSelector("sparc");
    if (unsupported.has_value()) {
        std::cerr << "TestUnsupportedSelector: sparc should be unsupported\n";
        return 1;
    }
    return 0;
}

int TestHintsFromIsa() {
    const auto x86_hints =
        tracelab::QemuSelectorHintsFromIsa("Advanced Micro Devices X86-64");
    const auto arm_hints = tracelab::QemuSelectorHintsFromIsa("AArch64");
    const auto rv_hints = tracelab::QemuSelectorHintsFromIsa("RISC-V");
    const auto unknown_hints = tracelab::QemuSelectorHintsFromIsa("PowerPC");

    if (x86_hints.size() != 1 || x86_hints[0] != "x86_64") {
        std::cerr << "TestHintsFromIsa: x86 hint mismatch\n";
        return 1;
    }
    if (arm_hints.size() != 1 || arm_hints[0] != "aarch64") {
        std::cerr << "TestHintsFromIsa: arm hint mismatch\n";
        return 1;
    }
    if (rv_hints.size() != 1 || rv_hints[0] != "riscv64") {
        std::cerr << "TestHintsFromIsa: riscv hint mismatch\n";
        return 1;
    }
    if (!unknown_hints.empty()) {
        std::cerr << "TestHintsFromIsa: unknown ISA should produce no hints\n";
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failed = 0;
    failed += TestNormalizeAliases();
    failed += TestUnsupportedSelector();
    failed += TestHintsFromIsa();

    if (failed == 0) {
        std::cout << "All qemu selector tests passed\n";
        return 0;
    }
    std::cerr << failed << " qemu selector tests failed\n";
    return 1;
}

