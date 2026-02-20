cmake_minimum_required(VERSION 3.16)

project(tracelab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(TRACELAB_BUILD_TESTS "Build TraceLab unit tests" ON)
option(TRACELAB_RUN_SCHEMA_TESTS "Run schema validation tests (requires python jsonschema)" OFF)

add_executable(tracelab
    src/main.cpp
    src/diagnosis/rules.cpp
    src/qemu.cpp
    src/util.cpp
    src/parsers/perf_parser.cpp
    src/parsers/strace_parser.cpp
    src/collectors/proc_status.cpp
    src/collectors/perf_stat.cpp
    src/collectors/strace_summary.cpp
    src/commands/doctor.cpp
    src/commands/run.cpp
    src/commands/report.cpp
    src/commands/inspect.cpp
    src/commands/compare.cpp
)

target_include_directories(tracelab PRIVATE include)

if (MSVC)
    target_compile_options(tracelab PRIVATE /W4 /permissive-)
else()
    target_compile_options(tracelab PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS tracelab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES schema/result.schema.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/tracelab/schema)

if (BUILD_TESTING AND TRACELAB_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    add_executable(tracelab_parser_tests
        src/qemu.cpp
        src/util.cpp
        src/parsers/perf_parser.cpp
        src/parsers/strace_parser.cpp
        tests/unit/parser_tests.cpp
    )
    target_include_directories(tracelab_parser_tests PRIVATE include)
    if (MSVC)
        target_compile_options(tracelab_parser_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tracelab_parser_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME parser_tests
        COMMAND tracelab_parser_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    add_executable(tracelab_diagnosis_tests
        src/diagnosis/rules.cpp
        src/util.cpp
        tests/unit/diagnosis_rules_tests.cpp
    )
    target_include_directories(tracelab_diagnosis_tests PRIVATE include)
    if (MSVC)
        target_compile_options(tracelab_diagnosis_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tracelab_diagnosis_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME diagnosis_rules_tests
        COMMAND tracelab_diagnosis_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    add_executable(tracelab_qemu_selector_tests
        src/qemu.cpp
        src/util.cpp
        tests/unit/qemu_selector_tests.cpp
    )
    target_include_directories(tracelab_qemu_selector_tests PRIVATE include)
    if (MSVC)
        target_compile_options(tracelab_qemu_selector_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tracelab_qemu_selector_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME qemu_selector_tests
        COMMAND tracelab_qemu_selector_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    if (Python3_Interpreter_FOUND)
        add_test(
            NAME run_nonzero_smoke
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/unit/run_nonzero_smoke.py
                    $<TARGET_FILE:tracelab>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        add_test(
            NAME compare_smoke
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/unit/compare_smoke.py
                    $<TARGET_FILE:tracelab>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        add_test(
            NAME run_qemu_invalid_arch_smoke
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/unit/run_qemu_invalid_arch_smoke.py
                    $<TARGET_FILE:tracelab>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        if (TRACELAB_RUN_SCHEMA_TESTS)
            add_test(
                NAME schema_validate_minimal
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_SOURCE_DIR}/scripts/validate_schema.py
                        --schema ${CMAKE_SOURCE_DIR}/schema/result.schema.json
                        --instance ${CMAKE_SOURCE_DIR}/tests/parser_fixtures/minimal_result.json
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            )
        endif()
    endif()
endif()
