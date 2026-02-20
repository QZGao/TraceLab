#include "tracelab/commands.h"
#include "tracelab/constants.h"
#include "tracelab/qemu.h"
#include "tracelab/util.h"

#include <iostream>
#include <sstream>

namespace tracelab {

// Implements `tracelab inspect`: lightweight ELF/ISA metadata extraction.
int HandleInspect(const std::vector<std::string> &args) {
    std::string json_path;
    std::string binary_path;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];
        if (arg == "--json") {
            if (i + 1 >= args.size()) {
                std::cerr << "inspect: --json expects a path\n";
                return 2;
            }
            json_path = args[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: tracelab inspect [--json <path>] <binary>\n";
            return 0;
        } else if (StartsWith(arg, "--")) {
            std::cerr << "inspect: unknown argument: " << arg << "\n";
            return 2;
        } else if (binary_path.empty()) {
            binary_path = arg;
        } else {
            std::cerr << "inspect: expected one binary path\n";
            return 2;
        }
    }

    if (binary_path.empty()) {
        std::cerr << "inspect: missing <binary>\n";
        return 2;
    }
    if (!FileExists(binary_path)) {
        std::cerr << "inspect: file not found: " << binary_path << "\n";
        return 2;
    }

    const bool has_readelf = CommandExists("readelf");
    const bool has_objdump = CommandExists("objdump");
    const bool has_llvm_objdump = CommandExists("llvm-objdump");
    const std::string disassembler =
        has_objdump ? "objdump" : (has_llvm_objdump ? "llvm-objdump" : "");

    std::vector<std::string> notes;
    std::string isa_arch = "unknown";
    std::string abi = "unknown";
    std::string linkage = "unknown";
    std::string symbols = "unknown";
    std::string plt_got = "unknown";
    std::string elf_type = "unknown";

    // Use readelf for stable structural metadata (header/program/sections/symbols).
    if (has_readelf) {
        const std::string qbin = ShellQuote(binary_path);

        const CommandResult header = RunCommandCapture("readelf -h " + qbin + " 2>&1");
        if (header.exit_code == 0) {
            isa_arch = ExtractLabeledField(header.output, "Machine:").value_or("unknown");
            abi = ExtractLabeledField(header.output, "OS/ABI:").value_or("unknown");
            elf_type = ExtractLabeledField(header.output, "Type:").value_or("unknown");
        } else {
            notes.push_back("readelf -h failed");
        }

        const CommandResult ph = RunCommandCapture("readelf -l " + qbin + " 2>&1");
        if (ph.exit_code == 0) {
            const std::string lower = ToLower(ph.output);
            if (lower.find("interp") != std::string::npos ||
                lower.find("dynamic") != std::string::npos) {
                linkage = "dynamic";
            } else {
                linkage = "static_or_unknown";
            }
        } else {
            notes.push_back("readelf -l failed");
        }

        const CommandResult sym = RunCommandCapture("readelf -s " + qbin + " 2>&1");
        if (sym.exit_code == 0) {
            const std::string lower = ToLower(sym.output);
            const bool has_symtab = lower.find("symbol table '.symtab'") != std::string::npos;
            const bool has_dynsym = lower.find("symbol table '.dynsym'") != std::string::npos;
            if (has_symtab) {
                symbols = "symtab_present";
            } else if (has_dynsym) {
                symbols = "dynsym_only_probably_stripped";
            } else {
                symbols = "no_symbols_detected";
            }
        } else {
            notes.push_back("readelf -s failed");
        }

        const CommandResult sec = RunCommandCapture("readelf -S " + qbin + " 2>&1");
        if (sec.exit_code == 0) {
            const std::string lower = ToLower(sec.output);
            const bool has_plt = lower.find(".plt") != std::string::npos;
            const bool has_got = lower.find(".got") != std::string::npos;
            plt_got = (has_plt || has_got) ? "present" : "not_detected";
        } else {
            notes.push_back("readelf -S failed");
        }
    } else {
        notes.push_back("readelf missing");
    }

    // Fallback linkage guess if program-header probing was inconclusive.
    if (linkage == "unknown") {
        const std::string type_lower = ToLower(elf_type);
        if (type_lower.find("dyn") != std::string::npos) {
            linkage = "dynamic_or_pie";
        } else if (type_lower.find("exec") != std::string::npos) {
            linkage = "exec_unknown_linkage";
        }
    }

    // Exercise disassembler availability; analysis output remains metadata-first.
    if (!disassembler.empty()) {
        if (RunCommandStatus(disassembler + " -d " + ShellQuote(binary_path) + NullRedirect()) != 0) {
            notes.push_back(disassembler + " -d failed");
        }
    } else {
        notes.push_back("objdump and llvm-objdump missing");
    }

    // Architecture hints that map directly to supported --qemu selectors.
    const std::vector<std::string> qemu_selector_hints = QemuSelectorHintsFromIsa(isa_arch);
    const std::vector<std::string> supported_selectors = SupportedQemuArchSelectors();

    std::cout << "TraceLab Inspect\n";
    std::cout << "  Binary: " << binary_path << "\n";
    std::cout << "  ISA/arch: " << isa_arch << "\n";
    std::cout << "  ABI: " << abi << "\n";
    std::cout << "  Linkage: " << linkage << "\n";
    std::cout << "  Symbols: " << symbols << "\n";
    std::cout << "  PLT/GOT: " << plt_got << "\n";
    std::cout << "  Disassembler: " << (disassembler.empty() ? "missing" : disassembler) << "\n";
    std::cout << "  QEMU selectors (supported): ";
    for (size_t i = 0; i < supported_selectors.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << supported_selectors[i];
    }
    std::cout << "\n";
    std::cout << "  QEMU selector hints: ";
    if (qemu_selector_hints.empty()) {
        std::cout << "none";
    } else {
        for (size_t i = 0; i < qemu_selector_hints.size(); ++i) {
            if (i > 0) {
                std::cout << ", ";
            }
            std::cout << qemu_selector_hints[i];
        }
    }
    std::cout << "\n";
    if (!notes.empty()) {
        std::cout << "  Notes:\n";
        for (const std::string &note : notes) {
            std::cout << "    - " << note << "\n";
        }
    }

    // Optional machine-readable artifact.
    if (!json_path.empty()) {
        std::ostringstream json;
        json << "{\n"
             << "  \"schema_version\": \"" << kSchemaVersion << "\",\n"
             << "  \"kind\": \"inspect_result\",\n"
             << "  \"timestamp_utc\": \"" << NowUtcIso8601() << "\",\n"
             << "  \"binary\": \"" << JsonEscape(binary_path) << "\",\n"
             << "  \"isa_arch\": \"" << JsonEscape(isa_arch) << "\",\n"
             << "  \"abi\": \"" << JsonEscape(abi) << "\",\n"
             << "  \"linkage\": \"" << JsonEscape(linkage) << "\",\n"
             << "  \"symbols\": \"" << JsonEscape(symbols) << "\",\n"
             << "  \"plt_got\": \"" << JsonEscape(plt_got) << "\",\n"
             << "  \"qemu_supported_selectors\": [";
        for (size_t i = 0; i < supported_selectors.size(); ++i) {
            if (i > 0) {
                json << ", ";
            }
            json << "\"" << JsonEscape(supported_selectors[i]) << "\"";
        }
        json << "],\n"
             << "  \"qemu_selector_hints\": [";
        for (size_t i = 0; i < qemu_selector_hints.size(); ++i) {
            if (i > 0) {
                json << ", ";
            }
            json << "\"" << JsonEscape(qemu_selector_hints[i]) << "\"";
        }
        json << "],\n"
             << "  \"disassembler\": \"" << JsonEscape(disassembler.empty() ? "missing" : disassembler)
             << "\",\n"
             << "  \"notes\": [";
        for (size_t i = 0; i < notes.size(); ++i) {
            if (i > 0) {
                json << ", ";
            }
            json << "\"" << JsonEscape(notes[i]) << "\"";
        }
        json << "]\n"
             << "}\n";

        std::string error;
        if (!WriteTextFile(json_path, json.str(), &error)) {
            std::cerr << "inspect: failed to write " << json_path << ": " << error << "\n";
            return 2;
        }
        std::cout << "  JSON: " << json_path << "\n";
    }

    return 0;
}

} // namespace tracelab
