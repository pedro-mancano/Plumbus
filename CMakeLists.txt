cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(plumbus C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DEV_USE_LOCAL_BACKUP "Use ./backup/cmd.exe in dev builds" ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(plumbus_core STATIC
    src/cmdline.c
    src/backend.c
    src/autorun.c
    src/proxy.c
    src/resolver.c
    src/state_sync.c
    src/exec.c
    src/cmd_expand.c
    src/cmd_ast.c
    src/builtins.c
    src/builtins_fs.c
    src/builtins_env.c
    src/builtins_sys.c
    src/cmd_runtime.c
    src/console.c
    src/history.c
    src/completion.c
    src/highlight.c
    src/line_editor.c
    src/git_native.c
    src/config.c
    src/prompt.c
    src/tui_config.c
)

target_include_directories(plumbus_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(plumbus_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(DEV_USE_LOCAL_BACKUP)
    target_compile_definitions(plumbus_core PUBLIC
        PLUMBUS_DEV_BACKUP_BACKEND=1
        PLUMBUS_DEV_BACKUP_PATH=L"${CMAKE_SOURCE_DIR}/backup/cmd.exe"
    )
endif()

if(MSVC)
    target_compile_options(plumbus_core PUBLIC
        /W4
        /WX
        /utf-8
        $<$<CONFIG:Release>:/O1 /Os /GL>
    )
    target_link_options(plumbus_core PUBLIC
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
else()
    target_compile_options(plumbus_core PUBLIC
        -Wall
        -Wextra
        -Werror
        $<$<CONFIG:Release>:-Os>
    )
    target_link_options(plumbus_core PUBLIC
        -static
        -municode
        $<$<CONFIG:Release>:-s>
    )
endif()

target_link_libraries(plumbus_core PUBLIC
    kernel32
    user32
    advapi32
    shlwapi
)

add_executable(plumbus src/main.c)
target_link_libraries(plumbus PRIVATE plumbus_core)
if(MSVC)
    target_link_options(plumbus PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
else()
    target_link_options(plumbus PRIVATE -static -municode $<$<CONFIG:Release>:-s>)
endif()

enable_testing()

function(plumbus_add_test test_name test_src)
    add_executable(${test_name} ${test_src})
    target_include_directories(${test_name} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(${test_name} PRIVATE plumbus_core)
    if(MSVC)
        target_link_options(${test_name} PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

plumbus_add_test(test_cmdline tests/test_cmdline.c)
plumbus_add_test(test_backend tests/test_backend.c)
plumbus_add_test(test_resolver tests/test_resolver.c)
plumbus_add_test(test_state_sync tests/test_state_sync.c)
plumbus_add_test(test_history tests/test_history.c)
plumbus_add_test(test_completion tests/test_completion.c)
plumbus_add_test(test_highlight tests/test_highlight.c)
plumbus_add_test(test_line_editor tests/test_line_editor.c)
plumbus_add_test(test_git_native tests/test_git_native.c)
plumbus_add_test(test_config tests/test_config.c)
plumbus_add_test(test_prompt tests/test_prompt.c)
plumbus_add_test(test_cmd_expand tests/test_cmd_expand.c)
plumbus_add_test(test_cmd_ast tests/test_cmd_ast.c)
plumbus_add_test(test_builtins tests/test_builtins.c)
plumbus_add_test(test_batch_runtime tests/test_batch_runtime.c)

add_executable(test_integration tests/test_integration.c)
target_include_directories(test_integration PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_integration PRIVATE plumbus_core)
target_compile_definitions(test_integration PRIVATE
    PLUMBUS_EXE_PATH=L"$<TARGET_FILE:plumbus>"
    PLUMBUS_TEST_SANDBOX_DIR=L"${CMAKE_BINARY_DIR}/test_sandbox"
)
if(MSVC)
    target_link_options(test_integration PRIVATE /SUBSYSTEM:CONSOLE)
endif()
add_test(NAME test_integration COMMAND test_integration)

add_executable(test_tui_integration tests/test_tui_integration.c)
target_include_directories(test_tui_integration PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_tui_integration PRIVATE plumbus_core)
target_compile_definitions(test_tui_integration PRIVATE
    PLUMBUS_EXE_PATH=L"$<TARGET_FILE:plumbus>"
    PLUMBUS_TEST_SANDBOX_DIR=L"${CMAKE_BINARY_DIR}/test_sandbox"
)
if(MSVC)
    target_link_options(test_tui_integration PRIVATE /SUBSYSTEM:CONSOLE)
endif()
add_test(NAME test_tui_integration COMMAND test_tui_integration)

add_executable(test_exec_edge_cases tests/test_exec_edge_cases.c)
target_include_directories(test_exec_edge_cases PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_exec_edge_cases PRIVATE plumbus_core)
target_compile_definitions(test_exec_edge_cases PRIVATE
    PLUMBUS_EXE_PATH=L"$<TARGET_FILE:plumbus>"
)
if(MSVC)
    target_link_options(test_exec_edge_cases PRIVATE /SUBSYSTEM:CONSOLE)
endif()
add_test(NAME test_exec_edge_cases COMMAND test_exec_edge_cases)

add_executable(test_cmd_parity tests/test_cmd_parity.c)
target_include_directories(test_cmd_parity PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_cmd_parity PRIVATE plumbus_core)
target_compile_definitions(test_cmd_parity PRIVATE
    PLUMBUS_EXE_PATH=L"$<TARGET_FILE:plumbus>"
    PLUMBUS_BACKUP_CMD_PATH=L"${CMAKE_SOURCE_DIR}/backup/cmd.exe"
    PLUMBUS_TEST_SANDBOX_DIR=L"${CMAKE_BINARY_DIR}/test_sandbox"
)
if(MSVC)
    target_link_options(test_cmd_parity PRIVATE /SUBSYSTEM:CONSOLE)
endif()
add_test(NAME test_cmd_parity COMMAND test_cmd_parity)
