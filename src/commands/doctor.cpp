#include "tracelab/commands.h"
#include "tracelab/constants.h"
#include "tracelab/util.h"

#include <iostream>
#include <sstream>

namespace tracelab {
    namespace {
        // Encodes a probe result as "found"/"missing" for CLI/JSON output.
        std::string ToolState(bool found) {
            return found ? "found" : "missing";
        }
    } // namespace

    // Implements `tracelab doctor`: probes toolchain/runtime dependencies.
    int HandleDoctor(const std::vector<std::string> &args) {
        std::string json_path;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string &arg = args[i];
            if (arg == "--json") {
                if (i + 1 >= args.size()) {
                    std::cerr << "doctor: --json expects a path\n";
                    return 2;
                }
                json_path = args[++i];
            } else if (arg == "--help") {
                std::cout << "Usage: tracelab doctor [--json <path>]\n";
                return 0;
            } else {
                std::cerr << "doctor: unknown argument: " << arg << "\n";
                return 2;
            }
        }

        const bool has_cmake = CommandExists("cmake");
        const bool has_ninja = CommandExists("ninja");
        const bool has_make = CommandExists("make");
        const bool has_clang = CommandExists("clang");
        const bool has_gcc = CommandExists("gcc");
        const bool has_ld = CommandExists("ld");
        const bool has_perf = CommandExists("perf");
        const bool has_strace = CommandExists("strace");

        const bool has_objdump = CommandExists("objdump");
        const bool has_llvm_objdump = CommandExists("llvm-objdump");
        const bool has_readelf = CommandExists("readelf");
        const bool has_nm = CommandExists("nm");
        const bool has_strip = CommandExists("strip");
        const bool has_gdb = CommandExists("gdb");
        const bool has_lldb = CommandExists("lldb");
        const bool has_qemu_x86_64 = CommandExists("qemu-x86_64");
        const bool has_qemu_aarch64 = CommandExists("qemu-aarch64");
        const bool has_qemu_riscv64 = CommandExists("qemu-riscv64");

        const bool has_builder = has_ninja || has_make;
        const bool has_compiler = has_clang || has_gcc;
        const bool has_disassembler = has_objdump || has_llvm_objdump;
        const bool missing_required =
                !(has_cmake && has_builder && has_compiler && has_ld && has_perf && has_strace);

        std::cout << "TraceLab Doctor\n";
        std::cout << "Host: " << HostOs() << " (" << HostArch() << ")\n\n";
        std::cout << "Required checks:\n";
        std::cout << "  cmake: " << ToolState(has_cmake) << "\n";
        std::cout << "  build backend (ninja|make): " << (has_builder ? "found" : "missing") << "\n";
        std::cout << "  compiler (clang|gcc): " << (has_compiler ? "found" : "missing") << "\n";
        std::cout << "  ld: " << ToolState(has_ld) << "\n";
        std::cout << "  perf: " << ToolState(has_perf) << "\n";
        std::cout << "  strace: " << ToolState(has_strace) << "\n\n";

        std::cout << "Optional checks:\n";
        std::cout << "  readelf: " << ToolState(has_readelf) << "\n";
        std::cout << "  disassembler (objdump|llvm-objdump): "
                << (has_disassembler ? "found" : "missing") << "\n";
        std::cout << "  nm: " << ToolState(has_nm) << "\n";
        std::cout << "  strip: " << ToolState(has_strip) << "\n";
        std::cout << "  qemu-x86_64: " << ToolState(has_qemu_x86_64) << "\n";
        std::cout << "  qemu-aarch64: " << ToolState(has_qemu_aarch64) << "\n";
        std::cout << "  qemu-riscv64: " << ToolState(has_qemu_riscv64) << "\n";
        std::cout << "  gdb: " << ToolState(has_gdb) << "\n";
        std::cout << "  lldb: " << ToolState(has_lldb) << "\n\n";
        std::cout << "Result: " << (missing_required ? "missing required tools" : "ready for baseline collection")
                << "\n";

        if (!json_path.empty()) {
            std::ostringstream json;
            json << "{\n"
                    << "  \"schema_version\": \"" << kSchemaVersion << "\",\n"
                    << "  \"kind\": \"doctor_result\",\n"
                    << "  \"timestamp_utc\": \"" << NowUtcIso8601() << "\",\n"
                    << "  \"host\": {\n"
                    << "    \"os\": \"" << HostOs() << "\",\n"
                    << "    \"arch\": \"" << HostArch() << "\"\n"
                    << "  },\n"
                    << "  \"required\": {\n"
                    << "    \"cmake\": \"" << ToolState(has_cmake) << "\",\n"
                    << "    \"build_backend\": \"" << (has_builder ? "found" : "missing") << "\",\n"
                    << "    \"compiler\": \"" << (has_compiler ? "found" : "missing") << "\",\n"
                    << "    \"ld\": \"" << ToolState(has_ld) << "\",\n"
                    << "    \"perf\": \"" << ToolState(has_perf) << "\",\n"
                    << "    \"strace\": \"" << ToolState(has_strace) << "\"\n"
                    << "  },\n"
                    << "  \"optional\": {\n"
                    << "    \"readelf\": \"" << ToolState(has_readelf) << "\",\n"
                    << "    \"disassembler\": \"" << (has_disassembler ? "found" : "missing") << "\",\n"
                    << "    \"nm\": \"" << ToolState(has_nm) << "\",\n"
                    << "    \"strip\": \"" << ToolState(has_strip) << "\",\n"
                    << "    \"qemu-x86_64\": \"" << ToolState(has_qemu_x86_64) << "\",\n"
                    << "    \"qemu-aarch64\": \"" << ToolState(has_qemu_aarch64) << "\",\n"
                    << "    \"qemu-riscv64\": \"" << ToolState(has_qemu_riscv64) << "\",\n"
                    << "    \"gdb\": \"" << ToolState(has_gdb) << "\",\n"
                    << "    \"lldb\": \"" << ToolState(has_lldb) << "\"\n"
                    << "  },\n"
                    << "  \"missing_required\": " << (missing_required ? "true" : "false") << "\n"
                    << "}\n";

            std::string error;
            if (!WriteTextFile(json_path, json.str(), &error)) {
                std::cerr << "doctor: failed to write " << json_path << ": " << error << "\n";
                return 2;
            }
            std::cout << "Doctor JSON written to " << json_path << "\n";
        }

        return missing_required ? 2 : 0;
    }
} // namespace tracelab