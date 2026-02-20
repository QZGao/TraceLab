#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tracelab {

// Canonical QEMU selector values accepted by `tracelab run --qemu <arch>`.
std::vector<std::string> SupportedQemuArchSelectors();

// Normalizes user-provided QEMU selector aliases (for example, amd64 -> x86_64).
// Returns nullopt for unsupported selectors.
std::optional<std::string> NormalizeQemuArchSelector(const std::string &selector);

// Validates/normalizes selector and returns wrapped argv (`qemu-<arch> <workload...>`).
// Returns false with an actionable error message when selector/bin is invalid.
bool BuildQemuWrappedCommand(const std::string &selector,
                             const std::vector<std::string> &workload_args,
                             std::vector<std::string> *wrapped_args,
                             std::string *normalized_arch, std::string *error);

// Derives likely QEMU selectors from readelf machine strings.
std::vector<std::string> QemuSelectorHintsFromIsa(const std::string &isa_arch);

} // namespace tracelab

