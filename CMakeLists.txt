cmake_minimum_required(VERSION 3.14)

set(IDF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sub/esp-idf")
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain.cmake")

# IDF needs these before project()
set(ENV{IDF_PATH} "${IDF_PATH}")

include("${IDF_PATH}/tools/cmake/project.cmake")

project(esp32-fps)

# Custom targets for flashing
find_program(ESPTOOL esptool.py
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/tools/esptool"
    NO_DEFAULT_PATH
)

set(PORT "/dev/ttyUSB0" CACHE STRING "Serial port for flashing")
set(BAUD "460800"       CACHE STRING "Flash baud rate")

add_custom_target(flash
    COMMAND ${ESPTOOL}
        --chip esp32
        --port ${PORT}
        --baud ${BAUD}
        write_flash @flash_args
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS ${CMAKE_PROJECT_NAME}.elf
    COMMENT "Flashing to ${PORT}"
)

add_custom_target(monitor
    COMMAND ${IDF_PATH}/tools/idf_monitor.py
        --port ${PORT}
        "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf"
    COMMENT "Opening serial monitor on ${PORT}"
)
