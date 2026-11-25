cmake_minimum_required(VERSION 3.24)

project(MiniSearch VERSION 0.1.0 LANGUAGES CXX)

option(MINISEARCH_ENABLE_ASAN "Enable AddressSanitizer on MiniSearch targets" OFF)
option(MINISEARCH_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer on MiniSearch targets" OFF)
option(MINISEARCH_ENABLE_CLANG_TIDY "Run clang-tidy on MiniSearch targets" OFF)
option(MINISEARCH_ENABLE_FUZZING "Build separate Clang libFuzzer targets" OFF)

set(MINISEARCH_VERSION "${PROJECT_VERSION}")

if(MINISEARCH_ENABLE_ASAN OR MINISEARCH_ENABLE_UBSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$" OR
       CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "Sanitizers require GCC or a Clang compiler with the GNU frontend")
    endif()
endif()

set(minisearch_sanitizer_flags)
if(MINISEARCH_ENABLE_ASAN)
    list(APPEND minisearch_sanitizer_flags -fsanitize=address)
endif()
if(MINISEARCH_ENABLE_UBSAN)
    list(APPEND minisearch_sanitizer_flags -fsanitize=undefined -fno-sanitize-recover=undefined)
endif()

if(MINISEARCH_ENABLE_CLANG_TIDY)
    find_program(MINISEARCH_CLANG_TIDY NAMES clang-tidy clang-tidy-19 clang-tidy-18 REQUIRED)
endif()

function(minisearch_configure_target target)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()

    if(minisearch_sanitizer_flags)
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer ${minisearch_sanitizer_flags})
        # Consumers of the static core must link the sanitizer runtimes as well.
        target_link_options(${target} PUBLIC ${minisearch_sanitizer_flags})
    endif()

    if(MINISEARCH_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${MINISEARCH_CLANG_TIDY}")
    endif()
endfunction()

set(minisearch_core_sources
    src/version.cpp
    src/text/tokenizer.cpp
    src/index/inverted_index.cpp
    src/ranking/bm25.cpp
    src/search/search_engine.cpp
    src/search/top_k.cpp
)
add_library(minisearch_core STATIC ${minisearch_core_sources})
target_compile_features(minisearch_core PUBLIC cxx_std_20)
target_include_directories(minisearch_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(minisearch_core PRIVATE MINISEARCH_VERSION="${MINISEARCH_VERSION}")
minisearch_configure_target(minisearch_core)

add_library(minisearch_cli STATIC
    src/cli/command_parser.cpp
    src/cli/file_reader.cpp
    src/cli/repl.cpp
)
target_link_libraries(minisearch_cli PUBLIC minisearch_core)
minisearch_configure_target(minisearch_cli)

add_executable(minisearch src/main.cpp)
target_link_libraries(minisearch PRIVATE minisearch_cli)
minisearch_configure_target(minisearch)

if(MINISEARCH_ENABLE_FUZZING)
    add_subdirectory(tests/fuzz)
endif()

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    set(INSTALL_GTEST OFF)
    set(BUILD_GMOCK OFF)
    set(gtest_force_shared_crt ON)
    FetchContent_Declare(
        googletest
        URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.15.2
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TLS_VERIFY TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(minisearch_tests
        tests/dynamic_array_test.cpp
        tests/hash_table_test.cpp
        tests/min_heap_test.cpp
        tests/tokenizer_test.cpp
        tests/inverted_index_test.cpp
        tests/bm25_test.cpp
        tests/top_k_test.cpp
        tests/search_engine_test.cpp
    )
    target_include_directories(minisearch_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(minisearch_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_tests)

    add_executable(minisearch_cli_tests
        tests/cli_parser_test.cpp
        tests/cli_repl_test.cpp
        tests/cli_file_reader_test.cpp
    )
    target_include_directories(minisearch_cli_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(minisearch_cli_tests PRIVATE minisearch_cli GTest::gtest_main)
    minisearch_configure_target(minisearch_cli_tests)

    # Keep the replacement array allocator isolated from the other tests.
    add_executable(minisearch_allocation_tests tests/hash_table_allocation_test.cpp)
    target_link_libraries(minisearch_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_allocation_tests)

    add_executable(minisearch_heap_allocation_tests tests/min_heap_allocation_test.cpp)
    target_link_libraries(minisearch_heap_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_heap_allocation_tests)

    add_executable(minisearch_tokenizer_allocation_tests tests/tokenizer_allocation_test.cpp)
    target_link_libraries(minisearch_tokenizer_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_tokenizer_allocation_tests)

    add_executable(minisearch_index_allocation_tests tests/inverted_index_allocation_test.cpp)
    target_link_libraries(minisearch_index_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_index_allocation_tests)

    add_executable(minisearch_bm25_allocation_tests tests/bm25_allocation_test.cpp)
    target_link_libraries(minisearch_bm25_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_bm25_allocation_tests)

    add_executable(minisearch_top_k_allocation_tests tests/top_k_allocation_test.cpp)
    target_include_directories(minisearch_top_k_allocation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(minisearch_top_k_allocation_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_top_k_allocation_tests)

    add_library(minisearch_test_reference STATIC tests/integration/reference_search.cpp)
    target_compile_features(minisearch_test_reference PUBLIC cxx_std_20)
    target_include_directories(minisearch_test_reference PUBLIC
        "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/tests/integration")
    minisearch_configure_target(minisearch_test_reference)

    add_executable(minisearch_integration_tests
        tests/integration/search_pipeline_test.cpp tests/integration/cli_session_test.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(minisearch_integration_tests PRIVATE tests/integration/cli_linux_io_test.cpp)
    endif()
    target_include_directories(minisearch_integration_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(minisearch_integration_tests PRIVATE
        minisearch_cli minisearch_test_reference GTest::gtest_main)
    minisearch_configure_target(minisearch_integration_tests)

    add_executable(minisearch_cli_allocation_tests tests/integration/cli_allocation_test.cpp)
    target_include_directories(minisearch_cli_allocation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(minisearch_cli_allocation_tests PRIVATE minisearch_cli GTest::gtest_main)
    minisearch_configure_target(minisearch_cli_allocation_tests)

    add_executable(minisearch_cli_fixture tests/integration/cli_fixture.cpp)
    target_link_libraries(minisearch_cli_fixture PRIVATE minisearch_test_reference)
    minisearch_configure_target(minisearch_cli_fixture)

    include(GoogleTest)
    gtest_discover_tests(minisearch_tests DISCOVERY_MODE PRE_TEST)
    gtest_discover_tests(minisearch_cli_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)
    foreach(target IN ITEMS minisearch_allocation_tests minisearch_heap_allocation_tests
            minisearch_tokenizer_allocation_tests minisearch_index_allocation_tests
            minisearch_bm25_allocation_tests minisearch_top_k_allocation_tests
            minisearch_cli_allocation_tests)
        gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS allocation TIMEOUT 60)
    endforeach()
    gtest_discover_tests(minisearch_integration_tests DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS integration TIMEOUT 90)

    file(WRITE "${PROJECT_BINARY_DIR}/cli-empty-stdin.txt" "")
    function(minisearch_add_cli_test name argument exit_code expected_text exact_match)
        add_test(
            NAME "cli.${name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCLI=$<TARGET_FILE:minisearch>"
                "-DARGUMENT=${argument}"
                "-DINPUT=${PROJECT_BINARY_DIR}/cli-empty-stdin.txt"
                "-DEXPECTED_EXIT=${exit_code}"
                "-DEXPECTED_TEXT=${expected_text}"
                "-DEXACT_MATCH=${exact_match}"
                -P "${PROJECT_SOURCE_DIR}/tests/check_cli.cmake"
        )
        set_tests_properties("cli.${name}" PROPERTIES TIMEOUT 30)
    endfunction()

    minisearch_add_cli_test(help --help 0 "Usage: minisearch" OFF)
    # Keep the release expectation independent of the compiled version definition.
    minisearch_add_cli_test(version --version 0 "minisearch 0.1.0" ON)
    minisearch_add_cli_test(unknown_option --unknown 2 "Unknown argument: --unknown" OFF)

    foreach(case IN ITEMS exit eof demo recovery invalid_arguments independent_processes escaped_argument)
        add_test(
            NAME "cli.session_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCLI=$<TARGET_FILE:minisearch>"
                "-DCASE=${case}"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/cli-session-tests/${case}"
                -P "${PROJECT_SOURCE_DIR}/tests/check_cli_session.cmake"
        )
        set_tests_properties("cli.session_${case}" PROPERTIES TIMEOUT 30)
    endforeach()

    foreach(case IN ITEMS lf crlf)
        add_test(NAME "integration.cli_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCLI=$<TARGET_FILE:minisearch>"
                "-DFIXTURE=$<TARGET_FILE:minisearch_cli_fixture>"
                "-DCASE=${case}"
                "-DWORK_DIR=${PROJECT_BINARY_DIR}/integration-fixtures/${case}"
                -P "${PROJECT_SOURCE_DIR}/tests/integration/check_end_to_end.cmake")
        set_tests_properties("integration.cli_${case}" PROPERTIES LABELS "integration;cli" TIMEOUT 45)
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(minisearch_closed_stdout tests/integration/closed_stdout.cpp)
        target_compile_features(minisearch_closed_stdout PRIVATE cxx_std_20)
        minisearch_configure_target(minisearch_closed_stdout)
        add_test(NAME integration.closed_stdout COMMAND minisearch_closed_stdout "$<TARGET_FILE:minisearch>")
        set_tests_properties(integration.closed_stdout PROPERTIES LABELS "integration;cli" TIMEOUT 20)
    endif()
endif()
