if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$" OR
   CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR "Fuzzing requires Clang with the GNU frontend and libFuzzer")
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer,address,undefined")
check_cxx_source_compiles([=[
    #include <cstddef>
    #include <cstdint>
    extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t) { return 0; }
]=] MINISEARCH_FUZZ_RUNTIME_LINKS)
cmake_pop_check_state()
if(NOT MINISEARCH_FUZZ_RUNTIME_LINKS)
    message(FATAL_ERROR "Clang could not compile and link with libFuzzer, ASan, and UBSan")
endif()

set(fuzz_compile_flags -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer)

function(minisearch_instrument_fuzz_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/tests/integration")
    minisearch_configure_target(${target})
    target_compile_options(${target} PRIVATE ${fuzz_compile_flags})
endfunction()

list(TRANSFORM minisearch_core_sources PREPEND "${PROJECT_SOURCE_DIR}/")
add_library(minisearch_fuzz_core STATIC ${minisearch_core_sources})
target_compile_definitions(minisearch_fuzz_core PRIVATE MINISEARCH_VERSION="${MINISEARCH_VERSION}")
minisearch_instrument_fuzz_target(minisearch_fuzz_core)

add_library(minisearch_fuzz_parser STATIC "${PROJECT_SOURCE_DIR}/src/cli/command_parser.cpp")
minisearch_instrument_fuzz_target(minisearch_fuzz_parser)

add_library(minisearch_fuzz_reference STATIC ../integration/reference_search.cpp)
minisearch_instrument_fuzz_target(minisearch_fuzz_reference)

foreach(name IN ITEMS tokenizer command_parser search_pipeline)
    add_executable(minisearch_${name}_fuzz ${name}_fuzz.cpp)
    minisearch_instrument_fuzz_target(minisearch_${name}_fuzz)
    target_link_options(minisearch_${name}_fuzz PRIVATE -fsanitize=fuzzer,address,undefined
        -fno-sanitize-recover=undefined)
endforeach()
target_link_libraries(minisearch_tokenizer_fuzz PRIVATE minisearch_fuzz_core minisearch_fuzz_reference)
target_link_libraries(minisearch_command_parser_fuzz PRIVATE minisearch_fuzz_parser)
target_link_libraries(minisearch_search_pipeline_fuzz PRIVATE minisearch_fuzz_core minisearch_fuzz_reference)
