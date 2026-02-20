#include "tracelab/qemu.h"
#include "tracelab/util.h"

namespace tracelab {

// Returns the canonical architecture selectors TraceLab supports for QEMU mode.
std::vector<std::string> SupportedQemuArchSelectors() {
    return {"x86_64", "aarch64", "riscv64"};
}

// Normalizes common user aliases to canonical QEMU selector names.
std::optional<std::string> NormalizeQemuArchSelector(const std::string &selector) {
    const std::string lower = ToLower(Trim(selector));
    if (lower == "x86_64" || lower == "amd64" || lower == "x64") {
        return "x86_64";
    }
    if (lower == "aarch64" || lower == "arm64") {
        return "aarch64";
    }
    if (lower == "riscv64" || lower == "riscv" || lower == "rv64") {
        return "riscv64";
    }
    return std::nullopt;
}

// Maps readelf-style ISA strings to likely QEMU selector hints.
std::vector<std::string> QemuSelectorHintsFromIsa(const std::string &isa_arch) {
    const std::string lower = ToLower(isa_arch);
    if (lower.find("x86-64") != std::string::npos || lower.find("x86_64") != std::string::npos) {
        return {"x86_64"};
    }
    if (lower.find("aarch64") != std::string::npos || lower.find("arm64") != std::string::npos) {
        return {"aarch64"};
    }
    if (lower.find("risc-v") != std::string::npos || lower.find("riscv") != std::string::npos) {
        return {"riscv64"};
    }
    return {};
}

} // namespace tracelab
