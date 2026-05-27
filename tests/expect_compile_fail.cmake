set(include_args "-I${SOURCE_DIR}")
foreach(include_dir IN LISTS INCLUDE_DIRS)
    list(APPEND include_args "-I${include_dir}")
endforeach()

execute_process(
    COMMAND
    "${CXX}"
    "-std=c++${STANDARD}"
    ${include_args}
    -fsyntax-only
    "${SOURCE_DIR}/${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

set(diagnostic "${output}\n${error}")
if(result EQUAL 0)
    message(FATAL_ERROR "expected ${SOURCE} to fail compilation")
endif()

if(NOT diagnostic MATCHES "${REGEX}")
    message(
        FATAL_ERROR
        "expected ${SOURCE} diagnostic to match '${REGEX}'\n${diagnostic}")
endif()
