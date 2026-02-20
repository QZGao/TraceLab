#include "tracelab/qemu.h"
#include "tracelab/util.h"

namespace tracelab {
    namespace {
        // Renders a comma-separated list for actionable error messages.
        std::string JoinCommaSeparated(const std::vector<std::string> &values) {
            std::string joined;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += values[i];
            }
            return joined;
        }
    } // namespace

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

    // Builds and validates the qemu-wrapped command argv for workload execution.
    bool BuildQemuWrappedCommand(const std::string &selector, const std::vector<std::string> &workload_args,
                                 std::vector<std::string> *wrapped_args,
                                 std::string *normalized_arch, std::string *error) {
        if (wrapped_args == nullptr || normalized_arch == nullptr) {
            if (error != nullptr) {
                *error = "internal error: null output pointers";
            }
            return false;
        }
        if (workload_args.empty()) {
            if (error != nullptr) {
                *error = "empty workload command";
            }
            return false;
        }

        const auto normalized = NormalizeQemuArchSelector(selector);
        if (!normalized.has_value()) {
            if (error != nullptr) {
                *error = "unsupported qemu architecture selector '" + selector +
                         "'; supported selectors: " + JoinCommaSeparated(SupportedQemuArchSelectors());
            }
            return false;
        }

        const std::string qemu_bin = "qemu-" + normalized.value();
        if (!CommandExists(qemu_bin)) {
            if (error != nullptr) {
                *error = "missing " + qemu_bin + " in PATH";
            }
            return false;
        }

        *normalized_arch = normalized.value();
        *wrapped_args = workload_args;
        wrapped_args->insert(wrapped_args->begin(), qemu_bin);
        return true;
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