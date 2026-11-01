# Usage: cmake -DCXX=<compiler> -DSRC=<codegen_probe.cc> -DINC=<include dir> -DARCH=<processor> -P check_codegen.cmake
#
# The branch-free lower bound is only branch-free if the compiler emits a conditional move for
# its select. LLVM turns a select on a loaded value into a compare and branch on x86-64 unless
# the condition is marked unpredictable, so this compiles the probe and looks for the move.
string(TOLOWER "${ARCH}" arch)
if(arch MATCHES "^(x86_64|amd64)$")
  set(pattern "[ \t]cmov")
elseif(arch MATCHES "^(arm64|aarch64)$")
  set(pattern "[ \t]csel[ \t]")
else()
  message(STATUS "codegen check skipped for ${ARCH}")
  return()
endif()

execute_process(
  COMMAND "${CXX}" -std=c++20 -O3 -DNDEBUG "-I${INC}" -S -o - "${SRC}"
  OUTPUT_VARIABLE asm
  ERROR_VARIABLE err
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "cannot compile ${SRC} to assembly: ${err}")
endif()
if(NOT asm MATCHES "docset_probe_lower_bound")
  message(FATAL_ERROR "the probe function is missing from the assembly")
endif()
if(NOT asm MATCHES "${pattern}")
  message(FATAL_ERROR "lower_bound_index compiled without a conditional move (${CXX}, ${ARCH}):\n${asm}")
endif()
message(STATUS "lower_bound_index uses a conditional move")
