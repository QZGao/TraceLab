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

// Derives likely QEMU selectors from readelf machine strings.
std::vector<std::string> QemuSelectorHintsFromIsa(const std::string &isa_arch);

} // namespace tracelab

