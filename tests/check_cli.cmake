execute_process(
    COMMAND "${CLI}" "${ARGUMENT}"
    INPUT_FILE "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 10
)

if(NOT "${result}" STREQUAL "${EXPECTED_EXIT}")
    message(FATAL_ERROR "Expected exit ${EXPECTED_EXIT}, got ${result}\n${stdout}${stderr}")
endif()

if(EXPECTED_EXIT EQUAL 0)
    set(output "${stdout}")
    if(NOT "${stderr}" STREQUAL "")
        message(FATAL_ERROR "Unexpected stderr\n${stderr}")
    endif()
else()
    set(output "${stderr}")
    if(NOT "${stdout}" STREQUAL "")
        message(FATAL_ERROR "Unexpected stdout\n${stdout}")
    endif()
endif()

if(EXACT_MATCH)
    if(NOT "${output}" STREQUAL "${EXPECTED_TEXT}\n")
        message(FATAL_ERROR "Expected exactly '${EXPECTED_TEXT}' followed by a newline\n${stdout}${stderr}")
    endif()
else()
    string(FIND "${output}" "${EXPECTED_TEXT}" match)
    if(match EQUAL -1)
        message(FATAL_ERROR "Expected '${EXPECTED_TEXT}' in output\n${stdout}${stderr}")
    endif()
endif()
