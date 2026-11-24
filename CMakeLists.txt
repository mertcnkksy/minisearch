cmake_minimum_required(VERSION 3.24)

project(MiniSearch VERSION 0.1.0 LANGUAGES CXX)

option(MINISEARCH_ENABLE_ASAN "Enable AddressSanitizer on MiniSearch targets" OFF)
option(MINISEARCH_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer on MiniSearch targets" OFF)
option(MINISEARCH_ENABLE_CLANG_TIDY "Run clang-tidy on MiniSearch targets" OFF)

set(MINISEARCH_VERSION "${PROJECT_VERSION}-dev")

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

add_library(minisearch_core STATIC src/version.cpp)
target_compile_features(minisearch_core PUBLIC cxx_std_20)
target_include_directories(minisearch_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(minisearch_core PRIVATE MINISEARCH_VERSION="${MINISEARCH_VERSION}")
minisearch_configure_target(minisearch_core)

add_executable(minisearch src/main.cpp)
target_link_libraries(minisearch PRIVATE minisearch_core)
minisearch_configure_target(minisearch)

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

    add_executable(minisearch_tests tests/dynamic_array_test.cpp)
    target_link_libraries(minisearch_tests PRIVATE minisearch_core GTest::gtest_main)
    minisearch_configure_target(minisearch_tests)

    include(GoogleTest)
    gtest_discover_tests(minisearch_tests DISCOVERY_MODE PRE_TEST)

    function(minisearch_add_cli_test name argument exit_code expected_text)
        add_test(
            NAME "cli.${name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCLI=$<TARGET_FILE:minisearch>"
                "-DARGUMENT=${argument}"
                "-DEXPECTED_EXIT=${exit_code}"
                "-DEXPECTED_TEXT=${expected_text}"
                -P "${PROJECT_SOURCE_DIR}/tests/check_cli.cmake"
        )
    endfunction()

    minisearch_add_cli_test(help --help 0 "Usage: minisearch")
    minisearch_add_cli_test(version --version 0 "minisearch ${MINISEARCH_VERSION}")
    minisearch_add_cli_test(unknown_option --unknown 2 "Unknown argument: --unknown")
endif()
