file(MAKE_DIRECTORY "${WORK_DIR}")
set(banner "MiniSearch\nIn-memory document search engine.\nType 'help' for available commands.\n\n")
set(prompt "minisearch> ")

function(check_session input exit_code expected_out expected_err)
    set(input_path "${WORK_DIR}/stdin.txt")
    file(WRITE "${input_path}" "${input}")
    execute_process(
        COMMAND "${CLI}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        INPUT_FILE "${input_path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 10
    )
    if(NOT "${result}" STREQUAL "${exit_code}")
        message(FATAL_ERROR "Expected exit ${exit_code}, got ${result}\n${stdout}\n${stderr}")
    endif()
    if(NOT "${stdout}" STREQUAL "${expected_out}")
        message(FATAL_ERROR "Unexpected stdout\nExpected [${expected_out}]\nActual [${stdout}]")
    endif()
    if(NOT "${stderr}" STREQUAL "${expected_err}")
        message(FATAL_ERROR "Unexpected stderr\nExpected [${expected_err}]\nActual [${stderr}]")
    endif()
endfunction()

if(CASE STREQUAL "exit")
    check_session("exit\nadd unread\n" 0 "${banner}${prompt}" "")
elseif(CASE STREQUAL "eof")
    check_session("" 0 "${banner}${prompt}" "")
    check_session("add hello" 0 "${banner}${prompt}Indexed document 0.\n" "")
elseif(CASE STREQUAL "demo")
    file(MAKE_DIRECTORY "${WORK_DIR}/documents")
    file(WRITE "${WORK_DIR}/documents/database notes.txt" "Database systems\n")
    string(CONCAT commands
        "add Python Python Python\n"
        "add Python supports data structures and algorithms\n"
        "index documents/database notes.txt\n"
        "search --top 2 python\nstats\nexit\n")
    string(CONCAT expected
        "${banner}"
        "${prompt}Indexed document 0.\n"
        "${prompt}Indexed document 1.\n"
        "${prompt}Indexed document 2.\n"
        "${prompt}Rank  Doc ID  Score\n1     0       0.768519\n2     1       0.372921\n\n"
        "Returned 2 results.\n${prompt}Documents:      3\nUnique terms:   8\n"
        "Total tokens:   11\nAvg. length:    3.666667\n${prompt}")
    check_session("${commands}" 0 "${expected}" "")
elseif(CASE STREQUAL "recovery")
    check_session("unknown\nadd hello\nexit\n" 0
        "${banner}${prompt}${prompt}Indexed document 0.\n${prompt}"
        "Error: Unknown command: unknown\n")
elseif(CASE STREQUAL "invalid_arguments")
    check_session("" 2 "" "Expected at most one argument. See minisearch --help.\n"
        --help --version)
elseif(CASE STREQUAL "independent_processes")
    check_session("add hello\nquit\n" 0 "${banner}${prompt}Indexed document 0.\n${prompt}" "")
    check_session("search hello\nexit\n" 0 "${banner}${prompt}No results.\n${prompt}" "")
elseif(CASE STREQUAL "escaped_argument")
    string(ASCII 27 13 10 127 155 control)
    check_session("" 2 "" "Unknown argument: bad\\x1B\\x0D\\x0A\\x7F\\x9B. See minisearch --help.\n"
        "bad${control}")
else()
    message(FATAL_ERROR "Unknown CLI smoke case '${CASE}'")
endif()
