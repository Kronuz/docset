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

  option(DOCSET_BENCH_REFERENCE "Fetch CRoaring as a benchmark-only reference" OFF)
  add_executable(docset_bench bench/bench.cc)
  target_link_libraries(docset_bench PRIVATE docset)
  if(DOCSET_BENCH_REFERENCE)
    # Benchmark-only: never part of the docset target.
    include(FetchContent)
    set(ENABLE_ROARING_TESTS OFF CACHE BOOL "" FORCE)
    set(ROARING_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(croaring
      GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
      GIT_TAG        master
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(croaring)
    target_link_libraries(docset_bench PRIVATE roaring)
    target_compile_definitions(docset_bench PRIVATE DOCSET_BENCH_REFERENCE=1)
  endif()
endif()
