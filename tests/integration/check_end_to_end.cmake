file(MAKE_DIRECTORY "${WORK_DIR}")

function(check_process scenario)
    execute_process(
        COMMAND "${FIXTURE}" "${scenario}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE fixture_result
        ERROR_VARIABLE fixture_errors
        TIMEOUT 10
    )
    if(NOT "${fixture_result}" STREQUAL "0")
        message(FATAL_ERROR "Fixture generation failed (${fixture_result})\n${fixture_errors}")
    endif()
    execute_process(
        COMMAND "${CLI}"
        WORKING_DIRECTORY "${WORK_DIR}"
        INPUT_FILE "${WORK_DIR}/session.in"
        OUTPUT_VARIABLE actual_out
        ERROR_VARIABLE actual_err
        RESULT_VARIABLE result
        TIMEOUT 10
    )
    file(READ "${WORK_DIR}/expected.out" expected_out)
    file(READ "${WORK_DIR}/expected.err" expected_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Expected exit 0, got ${result}\n${actual_out}\n${actual_err}")
    endif()
    if(NOT "${actual_err}" STREQUAL "${expected_err}")
        message(FATAL_ERROR "Unexpected stderr\nExpected [${expected_err}]\nActual [${actual_err}]")
    endif()
    if(NOT "${actual_out}" STREQUAL "${expected_out}")
        message(FATAL_ERROR "Unexpected stdout\nExpected [${expected_out}]\nActual [${actual_out}]")
    endif()
endfunction()

check_process("${CASE}")
if(CASE STREQUAL "lf")
    check_process(fresh)
endif()
