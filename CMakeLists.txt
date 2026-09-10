cmake_minimum_required(VERSION 3.30)
project(11vated_digital_console VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

# ---------------------------------------------------------------------------
# runtime/core — host-agnostic platform models and interfaces (canon §39.1)
# ---------------------------------------------------------------------------
add_library(dc_core STATIC
    runtime/core/dc/capability_json.cpp
    runtime/core/dc/qualification.cpp
    runtime/core/dc/qualification_json.cpp
    runtime/core/dc/json.cpp
    runtime/core/dc/session.cpp
    runtime/core/dc/title_registry.cpp
    runtime/core/dc/focus_graph.cpp
)
target_include_directories(dc_core PUBLIC runtime/core)
target_compile_features(dc_core PUBLIC cxx_std_23)

# ---------------------------------------------------------------------------
# runtime/host — host abstraction interface layer
# ---------------------------------------------------------------------------
add_library(dc_host_interface INTERFACE)
target_include_directories(dc_host_interface INTERFACE runtime/host)
target_link_libraries(dc_host_interface INTERFACE dc_core)

# ---------------------------------------------------------------------------
# hosts/windows — Windows Host Edition implementation (ADR-0002)
# ---------------------------------------------------------------------------
if (WIN32)
    add_library(dc_host_windows STATIC
        hosts/windows/host_platform.cpp
        hosts/windows/probe_system.cpp
        hosts/windows/probe_cpugpu.cpp
        hosts/windows/probe_display.cpp
        hosts/windows/probe_devices.cpp
        hosts/windows/probe_storage.cpp
        hosts/windows/probe_directstorage.cpp
        hosts/windows/probe_vulkan.cpp
        hosts/windows/probe_gameinput.cpp
        hosts/windows/probe_sdl3.cpp
        hosts/windows/title_supervisor.cpp
        hosts/windows/input_router.cpp
        hosts/windows/guids.cpp
        hosts/windows/bench/bench_cpu.cpp
        hosts/windows/bench/bench_memory.cpp
        hosts/windows/bench/bench_storage.cpp
        hosts/windows/bench/bench_gpu.cpp
        hosts/windows/bench/qualify_runner.cpp
    )
    target_include_directories(dc_host_windows PUBLIC hosts/windows tools/gameinput/native/include)
    target_link_libraries(dc_host_windows PUBLIC dc_core dc_host_interface
        dxgi d3d12 d3dcompiler ole32 uuid xinput user32 gdi32 advapi32 tbs version
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gameinput/native/lib/x64/GameInput.lib")
    target_include_directories(dc_host_windows PUBLIC tools/sdl3/SDL3-3.4.16/include)
    target_link_directories(dc_host_windows PUBLIC tools/sdl3/SDL3-3.4.16/lib/x64)
    target_link_libraries(dc_host_windows PUBLIC SDL3)
endif()

# ---------------------------------------------------------------------------
# tools/hostprof — dc-hostprof, the Host Doctor (first executable, canon §50)
# ---------------------------------------------------------------------------
if (WIN32)
    add_executable(dc-hostprof tools/hostprof/main.cpp)
    target_link_libraries(dc-hostprof PRIVATE dc_core dc_host_windows)
    target_compile_definitions(dc-hostprof PRIVATE DC_BUILD_GIT_HASH="session-2026-09-06")

    # -----------------------------------------------------------------------
    # tools/displayprobe — dc-displayprobe, presentation-path qualification
    # -----------------------------------------------------------------------
    add_executable(dc-displayprobe tools/displayprobe/main.cpp)
    target_link_libraries(dc-displayprobe PRIVATE dc_core)
    target_compile_definitions(dc-displayprobe PRIVATE DC_BUILD_GIT_HASH="session-2026-09-06")

    # -----------------------------------------------------------------------
    # tools/qualreport — dc-qualify-report, conservative evidence aggregator
    # -----------------------------------------------------------------------
    add_executable(dc-qualify-report tools/qualreport/main.cpp)

    # -----------------------------------------------------------------------
    # tools/stagecert — dc-stage-certification, quiet-system staging runner
    # -----------------------------------------------------------------------
    add_executable(dc-stage-certification tools/stagecert/main.cpp)

    # -----------------------------------------------------------------------
    # samples/native-minimal — the first real native Digital Console title
    # (DK0-M2 §29). WinMain + D3D12; launched by dc-title-supervisor.
    # -----------------------------------------------------------------------
    add_executable(dc-sample-native-minimal WIN32 samples/native-minimal/title_main.cpp)
    target_link_libraries(dc-sample-native-minimal PRIVATE dc_core dc_host_windows dxgi d3d12 d3dcompiler)

    # -----------------------------------------------------------------------
    # tools/session — dc-session, the console session runtime (DK0-M2)
    # -----------------------------------------------------------------------
    add_executable(dc-session tools/session/main.cpp tools/session/shell.cpp)
    target_link_libraries(dc-session PRIVATE dc_core dc_host_windows dxgi d3d12)
    # Interactive-shell selftest: boots, renders N frames, exits cleanly.
    add_test(NAME session_shell_selftest
             COMMAND dc-session --shell --selftest 90
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(session_shell_selftest PROPERTIES ENVIRONMENT
        "DC_REGISTRY_DIR=${CMAKE_SOURCE_DIR}/registry;DC_JOURNAL_DIR=${CMAKE_SOURCE_DIR}/evidence/sessions")
    target_link_libraries(dc-qualify-report PRIVATE dc_core)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if (WIN32)
    add_executable(test_capability_contract tests/contract/test_capability_contract.cpp)
    target_link_libraries(test_capability_contract PRIVATE dc_core)

    add_executable(test_profile_derivation tests/contract/test_profile_derivation.cpp)
    target_link_libraries(test_profile_derivation PRIVATE dc_core)

    add_executable(test_json_parser tests/contract/test_json_parser.cpp)
    target_link_libraries(test_json_parser PRIVATE dc_core)

    add_executable(test_session_state tests/contract/test_session_state.cpp)
    target_link_libraries(test_session_state PRIVATE dc_core)

    add_test(NAME capability_contract COMMAND test_capability_contract)
    add_test(NAME profile_derivation COMMAND test_profile_derivation)
    add_test(NAME json_parser COMMAND test_json_parser)
    add_test(NAME session_state COMMAND test_session_state)

    add_executable(test_supervisor_live tests/contract/test_supervisor_live.cpp)
    target_link_libraries(test_supervisor_live PRIVATE dc_core dc_host_windows)
    add_test(NAME supervisor_live COMMAND test_supervisor_live)

    add_executable(test_input_router_live tests/contract/test_input_router_live.cpp)
    target_link_libraries(test_input_router_live PRIVATE dc_core dc_host_windows)
    add_test(NAME input_router_live COMMAND test_input_router_live)

    add_executable(test_title_context tests/contract/test_title_context.cpp)
    target_link_libraries(test_title_context PRIVATE dc_core dc_host_windows)
    add_test(NAME title_context COMMAND test_title_context)

    add_executable(test_input_reconcile tests/contract/test_input_reconcile.cpp)
    target_link_libraries(test_input_reconcile PRIVATE dc_core)
    add_test(NAME input_reconcile COMMAND test_input_reconcile)

    add_executable(test_focus_graph tests/contract/test_focus_graph.cpp)
    target_link_libraries(test_focus_graph PRIVATE dc_core)
    add_test(NAME focus_graph COMMAND test_focus_graph)
endif()
