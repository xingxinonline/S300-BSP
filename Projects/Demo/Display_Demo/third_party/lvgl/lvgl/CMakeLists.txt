# Placeholder to allow offline build if network is unavailable.
# You can drop the official LVGL sources here: third_party/lvgl/lvgl/
# For now, error with a clear message.
cmake_minimum_required(VERSION 3.16)
project(lvgl-placeholder NONE)
message(FATAL_ERROR "LVGL sources not vendored. Please clone lvgl into: ${CMAKE_CURRENT_LIST_DIR} or set -DLVGL_LOCAL_PATH=/path/to/lvgl or enable network for FetchContent.")
