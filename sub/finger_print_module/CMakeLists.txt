set(FPM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/FingerPrintModule")

idf_component_register(
  SRCS
    "${FPM_ROOT}/src/fpm.c"
    "${FPM_ROOT}/src/sensor_driver_r503.c"
    "${FPM_ROOT}/sub/r503/src/r503.c"
    "${FPM_ROOT}/sub/r503/src/r503_pkg.c"

  INCLUDE_DIRS
    "${FPM_ROOT}/src"
    "${FPM_ROOT}/sub/r503/src"
)
