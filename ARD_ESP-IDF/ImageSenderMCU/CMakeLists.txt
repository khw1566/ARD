cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ard_image_sender)

# Generate a rate-limited camera HAL source without editing managed downloads.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/camera_diagnostics.cmake")
