cmake_minimum_required(VERSION 3.14)
project(docset LANGUAGES CXX)

# Header-only: docset.hh is the whole implementation.
add_library(docset INTERFACE)
target_include_directories(docset INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(docset INTERFACE cxx_std_20)
add_library(docset::docset ALIAS docset)

# Tests and benchmarks build only when this repo is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(docset_test test/test.cc)
  target_link_libraries(docset_test PRIVATE docset)
  add_test(NAME docset COMMAND docset_test)
  add_executable(docset_portable_test test/test.cc)
  target_link_libraries(docset_portable_test PRIVATE docset)
  target_compile_definitions(docset_portable_test PRIVATE DOCSET_NO_SIMD)
  add_test(NAME docset_portable COMMAND docset_portable_test)

  add_executable(docset_diff test/diff.cc)
  target_link_libraries(docset_diff PRIVATE docset)
  target_include_directories(docset_diff PRIVATE test)
  add_test(NAME docset_diff COMMAND docset_diff)
  # The branch-free lower bound must compile to a conditional move (checked on x86-64 and arm64).
  add_test(NAME docset_codegen COMMAND ${CMAKE_COMMAND}
    -DCXX=${CMAKE_CXX_COMPILER} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/test/codegen_probe.cc
    -DINC=${CMAKE_CURRENT_SOURCE_DIR} -DARCH=${CMAKE_SYSTEM_PROCESSOR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_codegen.cmake)
  add_executable(docset_cmp_bench bench/cmp_bench.cc)
  target_link_libraries(docset_cmp_bench PRIVATE docset)
  target_include_directories(docset_cmp_bench PRIVATE test)
  # The same rows against the reference, one version per binary: bench/ab_compare.py.
  add_executable(docset_ab_new bench/ab_bench.cc)
  target_link_libraries(docset_ab_new PRIVATE docset)
  add_executable(docset_ab_old bench/ab_bench.cc)
  target_link_libraries(docset_ab_old PRIVATE docset)
  target_include_directories(docset_ab_old PRIVATE test)
  target_compile_definitions(docset_ab_old PRIVATE DOCSET_AB_OLD)

  option(DOCSET_BENCH_REFERENCE "Fetch CRoaring as a benchmark-only reference" OFF)
  # libFuzzer target (Clang): cmake -DDOCSET_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
  option(DOCSET_FUZZ "Build the libFuzzer target" OFF)
  if(DOCSET_FUZZ)
    add_executable(docset_fuzz fuzz/fuzz_frozen.cc)
    target_link_libraries(docset_fuzz PRIVATE docset)
    target_compile_options(docset_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(docset_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
  add_executable(docset_bench bench/bench.cc)
  target_link_libraries(docset_bench PRIVATE docset)
  if(DOCSET_BENCH_REFERENCE)
    # Benchmark-only: never part of the docset target.
    include(FetchContent)
    set(ENABLE_ROARING_TESTS OFF CACHE BOOL "" FORCE)
    set(ROARING_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(croaring
      GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
      GIT_TAG        db0d1e9ae84041b64fa068891db5ca5f80773e1b)  # v5.4.0, the version the README numbers use
    FetchContent_MakeAvailable(croaring)
    target_link_libraries(docset_bench PRIVATE roaring)
    target_compile_definitions(docset_bench PRIVATE DOCSET_BENCH_REFERENCE=1)
  endif()
endif()
