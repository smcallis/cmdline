cmake_minimum_required(VERSION 3.20)

project(cmdline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_custom_target(
    copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Symlinking compile_commands.json to source root"
    VERBATIM)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)

add_subdirectory(fmt)

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

    function(add_cmdline_compile_fail_test name source)
        add_test(
            NAME ${name}
            COMMAND
            ${CMAKE_COMMAND}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DSTANDARD=${CMAKE_CXX_STANDARD}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DSOURCE=${source}
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
