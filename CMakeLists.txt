cmake_minimum_required(VERSION 3.20)

project(cmdline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(fmt REQUIRED CONFIG)

add_custom_target(
    copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Symlinking compile_commands.json to source root"
    VERBATIM)

set(
    CMDLINE_FORMAT_FILES
    cmdline.h
    example.cc
    internal/cmdline_check.h
    internal/cmdline_error.h
    internal/cmdline_faces.h
    internal/cmdline_match.h
    internal/cmdline_model.h
    internal/cmdline_parse.h
    internal/cmdline_print.h
    internal/cmdline_rules.h
    internal/cmdline_typed.h
    internal/cmdline_types.h
    internal/cmdline_utils.h
    tests/cmdline_parser_test.cc
    tests/cmdline_test.cc
    tests/cmdline_test_helpers.h
    tests/compile_fail/int_invalid_default.cc
    tests/compile_fail/uint_invalid_default.cc)

find_program(CLANG_FORMAT_EXE NAMES clang-format-20)

if(CLANG_FORMAT_EXE)
    add_custom_target(
        format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${CMDLINE_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)
else()
    add_custom_target(
        format-check
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format-20 was not found"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM)
endif()

add_library(cmdline INTERFACE)
target_include_directories(cmdline INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cmdline INTERFACE fmt::fmt)
target_compile_features(cmdline INTERFACE cxx_std_23)

add_executable(example example.cc)
target_link_libraries(example PRIVATE cmdline)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(
        cmdline_tests
        tests/cmdline_parser_test.cc
        tests/cmdline_test.cc)
    target_link_libraries(
        cmdline_tests
        PRIVATE
        cmdline
        GTest::gtest
        GTest::gtest_main)

    gtest_discover_tests(cmdline_tests)

    get_target_property(
        CMDLINE_FMT_INCLUDE_DIRS fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT CMDLINE_FMT_INCLUDE_DIRS)
        set(CMDLINE_FMT_INCLUDE_DIRS "")
    endif()

    function(add_cmdline_compile_fail_test name source)
        add_test(
            NAME ${name}
            COMMAND
            ${CMAKE_COMMAND}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DSTANDARD=${CMAKE_CXX_STANDARD}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DSOURCE=${source}
            "-DINCLUDE_DIRS=${CMDLINE_FMT_INCLUDE_DIRS}"
            "-DREGEX=invalid command-line specification"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_compile_fail.cmake)
    endfunction()

    add_cmdline_compile_fail_test(
        CmdlineCompileFail.IntInvalidDefault
        tests/compile_fail/int_invalid_default.cc)
    add_cmdline_compile_fail_test(
        CmdlineCompileFail.UintInvalidDefault
        tests/compile_fail/uint_invalid_default.cc)
endif()
