execute_process(
    COMMAND "${CLI}" "${ARGUMENT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT "${result}" STREQUAL "${EXPECTED_EXIT}")
    message(FATAL_ERROR "Expected exit ${EXPECTED_EXIT}, got ${result}\n${stdout}${stderr}")
endif()

if(EXPECTED_EXIT EQUAL 0)
    set(output "${stdout}")
else()
    set(output "${stderr}")
endif()
string(FIND "${output}" "${EXPECTED_TEXT}" match)
if(match EQUAL -1)
    message(FATAL_ERROR "Expected '${EXPECTED_TEXT}' in output\n${stdout}${stderr}")
endif()
