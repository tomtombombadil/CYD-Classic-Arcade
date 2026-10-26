# The plain C++ parts (emu/ and core/), built for the board. The same files
# are built on the PC by tools/host and tools/host_tests.
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
file(GLOB_RECURSE EMU_SRCS ${ROOT}/emu/*.cpp)
file(GLOB CORE_SRCS ${ROOT}/core/*.cpp)
idf_component_register(
    SRCS ${EMU_SRCS} ${CORE_SRCS}
    INCLUDE_DIRS ${ROOT})
# -freorder-blocks-and-partition: on the Xtensa no hot / cold sections are
# made, but each function's rare blocks (the callbacks of the CPU cores'
# memory accesses, interrupt paths) go to its end, so the usual path runs
# straight through, without taken jumps, and the code that runs every frame
# fills fewer instruction cache lines (the ESP32-S3 has 16 KB). Measured on
# Gyruss with an instruction-level model of the S3 (2026-10-24): ~6 % fewer
# cycles outside cache misses, ~15 % fewer misses; Elevator Action, Donkey
# Kong Jr. and 1942 gain too.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-parameter -freorder-blocks-and-partition)
