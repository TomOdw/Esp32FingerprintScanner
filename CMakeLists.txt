cmake_minimum_required(VERSION 3.14)

set(IDF_PATH "~/.espressif/v6.0.1/esp-idf")
set(ENV{IDF_PATH} "${IDF_PATH}")

include("${IDF_PATH}/tools/cmake/project.cmake")

set(EXTRA_COMPONENT_DIRS "src")

project(esp32-fps)
