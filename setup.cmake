# setup.cmake — run with: cmake -P setup.cmake
cmake_minimum_required(VERSION 3.14)

set(TOOLCHAIN_VERSION "15.2.0_20251204")
set(IDF_VERSION "v6.0.1")

# Download xtensa toolchain
set(TOOLCHAIN_URL
    "https://github.com/espressif/crosstool-NG/releases/download/esp-${TOOLCHAIN_VERSION}/xtensa-esp-elf-${TOOLCHAIN_VERSION}-x86_64-linux-gnu.tar.xz"
)
set(TOOLCHAIN_ARCHIVE "${CMAKE_CURRENT_LIST_DIR}/toolchain.tar.xz")
set(TOOLCHAIN_DIR     "${CMAKE_CURRENT_LIST_DIR}")

message(STATUS "Downloading toolchain...")
file(DOWNLOAD
    ${TOOLCHAIN_URL}
    ${TOOLCHAIN_ARCHIVE}
    SHOW_PROGRESS
    STATUS download_status
)

list(GET download_status 0 status_code)
if(NOT status_code EQUAL 0)
    message(FATAL_ERROR "Download failed: ${download_status}")
endif()

message(STATUS "Extracting...")
file(ARCHIVE_EXTRACT
    INPUT      ${TOOLCHAIN_ARCHIVE}
    DESTINATION ${TOOLCHAIN_DIR}
)

file(REMOVE ${TOOLCHAIN_ARCHIVE})
message(STATUS "Toolchain ready in tools/")

# Download esp-idf
set(IDF_URL "https://github.com/espressif/esp-idf/archive/refs/tags/${IDF_VERSION}.tar.gz")
set(IDF_ARCHIVE "${CMAKE_CURRENT_LIST_DIR}/sub/esp-idf.tar.gz")
set(IDF_DIR "${CMAKE_CURRENT_LIST_DIR}/sub/esp-idf-${IDF_VERSION}")

if(EXISTS "${IDF_DIR}")
    message(STATUS "esp-idf already present, skipping download")
    return()
endif()

message(STATUS "Downloading esp-idf ${IDF_VERSION}...")
file(DOWNLOAD ${IDF_URL} ${IDF_ARCHIVE} SHOW_PROGRESS STATUS status)
list(GET status 0 status_code)
if(NOT status_code EQUAL 0)
    message(FATAL_ERROR "Download failed: ${status}")
endif()

message(STATUS "Extracting...")
file(ARCHIVE_EXTRACT INPUT ${IDF_ARCHIVE} DESTINATION ${CMAKE_CURRENT_LIST_DIR})
file(REMOVE ${IDF_ARCHIVE})

message(STATUS "esp-idf ${IDF_VERSION} ready")
