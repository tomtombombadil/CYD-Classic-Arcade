# The plain C++ parts (emu/ and core/), built for the board. The same files
# are built on the PC by tools/host and tools/host_tests.
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
file(GLOB_RECURSE EMU_SRCS ${ROOT}/emu/*.cpp)
file(GLOB CORE_SRCS ${ROOT}/core/*.cpp)
idf_component_register(
    SRCS ${EMU_SRCS} ${CORE_SRCS}
    INCLUDE_DIRS ${ROOT})
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-parameter)
