# NOTE: The tools are organized into groups of four consisting of one large and
# three small executables. This is done to minimize memory load in parallel
# builds.  Please retain this ordering.

if( NOT WIN32 OR MSYS OR CYGWIN )
  # We currently require 'sed' to build llvm-config, so don't try to build it
  # on pure Win32.
  add_subdirectory(../llvm/tools/llvm-config ${CMAKE_CURRENT_BINARY_DIR}/llvm-config)
endif()

add_subdirectory(opt)
add_subdirectory(../llvm/tools/llvm-as ${CMAKE_CURRENT_BINARY_DIR}/llvm-as)
add_subdirectory(../llvm/tools/llvm-dis ${CMAKE_CURRENT_BINARY_DIR}/llvm-dis)
add_subdirectory(../llvm/tools/llvm-mc ${CMAKE_CURRENT_BINARY_DIR}/llvm-mc)

add_subdirectory(../llvm/tools/llc ${CMAKE_CURRENT_BINARY_DIR}/llc)
add_subdirectory(../llvm/tools/llvm-ranlib ${CMAKE_CURRENT_BINARY_DIR}/llvm-ranlib)
add_subdirectory(../llvm/tools/llvm-ar ${CMAKE_CURRENT_BINARY_DIR}/llvm-ar)
add_subdirectory(../llvm/tools/llvm-nm ${CMAKE_CURRENT_BINARY_DIR}/llvm-nm)
add_subdirectory(../llvm/tools/llvm-size ${CMAKE_CURRENT_BINARY_DIR}/llvm-size)

add_subdirectory(../llvm/tools/llvm-cov ${CMAKE_CURRENT_BINARY_DIR}/llvm-cov)
add_subdirectory(../llvm/tools/llvm-prof ${CMAKE_CURRENT_BINARY_DIR}/llvm-prof)
add_subdirectory(../llvm/tools/llvm-link ${CMAKE_CURRENT_BINARY_DIR}/llvm-link)
add_subdirectory(../llvm/tools/lli ${CMAKE_CURRENT_BINARY_DIR}/lli)

add_subdirectory(../llvm/tools/llvm-extract ${CMAKE_CURRENT_BINARY_DIR}/llvm-extract)
add_subdirectory(../llvm/tools/llvm-diff ${CMAKE_CURRENT_BINARY_DIR}/llvm-diff)
add_subdirectory(../llvm/tools/macho-dump ${CMAKE_CURRENT_BINARY_DIR}/macho-dump)
add_subdirectory(../llvm/tools/llvm-objdump ${CMAKE_CURRENT_BINARY_DIR}/llvm-objdump)
add_subdirectory(../llvm/tools/llvm-readobj ${CMAKE_CURRENT_BINARY_DIR}/llvm-readobj)
add_subdirectory(../llvm/tools/llvm-rtdyld ${CMAKE_CURRENT_BINARY_DIR}/llvm-rtdyld)
add_subdirectory(../llvm/tools/llvm-dwarfdump ${CMAKE_CURRENT_BINARY_DIR}/llvm-dwarfdump)
if( LLVM_USE_INTEL_JITEVENTS )
  add_subdirectory(../llvm/tools/llvm-jitlistener ${CMAKE_CURRENT_BINARY_DIR}/llvm-jitlistener)
endif( LLVM_USE_INTEL_JITEVENTS )

add_subdirectory(bugpoint)
add_subdirectory(../llvm/tools/bugpoint-passes ${CMAKE_CURRENT_BINARY_DIR}/bugpoint-passes)
add_subdirectory(../llvm/tools/llvm-bcanalyzer ${CMAKE_CURRENT_BINARY_DIR}/llvm-bcanalyzer)
add_subdirectory(../llvm/tools/llvm-stress ${CMAKE_CURRENT_BINARY_DIR}/llvm-stress)
add_subdirectory(../llvm/tools/llvm-mcmarkup ${CMAKE_CURRENT_BINARY_DIR}/llvm-mcmarkup)

add_subdirectory(../llvm/tools/llvm-symbolizer ${CMAKE_CURRENT_BINARY_DIR}/llvm-symbolizer)

if( NOT WIN32 )
  add_subdirectory(../llvm/tools/lto ${CMAKE_CURRENT_BINARY_DIR}/lto)
endif()

if( LLVM_ENABLE_PIC )
  # TODO: support other systems:
  if( (CMAKE_SYSTEM_NAME STREQUAL "Linux")
      OR (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD") )
    add_subdirectory(../llvm/tools/gold ${CMAKE_CURRENT_BINARY_DIR}/gold)
  endif()
endif()

# XXX: Ugly fix for adding our own passes to clang
set(LLVM_TARGETS_TO_BUILD ${LLVM_TARGETS_TO_BUILD}
  pur pta pa noalias cca dse static-profiler utils function-fusion DataStructure
  )

add_llvm_external_project(clang)
add_llvm_external_project(lld)
add_llvm_external_project(lldb)
add_llvm_external_project(polly)

set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} PARENT_SCOPE)
