# ESP-IDF project file (PlatformIO runs it; see platformio.ini).
# Outside code lives in external/, downloaded by tools/fetch_deps.py.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(ENV{BTSTACK_ROOT} ${CMAKE_CURRENT_SOURCE_DIR}/external/btstack)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/external/bluepad32/src/components
    ${CMAKE_CURRENT_SOURCE_DIR}/external/LovyanGFX)

# A board's own sdkconfig defaults on top of the shared ones: its env passes
# -DCYD_SDKCONFIG_EXTRA=<file> (platformio.ini board_build.cmake_extra_args).
# ESP-IDF reads them in order, each followed by its .<target> file if there
# is one: sdkconfig.defaults, sdkconfig.defaults.esp32s3, then the board's
# (later values win).
if(CYD_SDKCONFIG_EXTRA)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/${CYD_SDKCONFIG_EXTRA}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Adds BTstack's ESP32 port as a component (after project.cmake, before project())
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)

project(cyd_classic_arcade)
