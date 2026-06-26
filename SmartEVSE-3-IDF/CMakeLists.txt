# =============================================================================
# SmartEVSE-3 (v3) — native ESP-IDF v6.0.1 project
# =============================================================================
# Replaces the old platformio.ini build.
# Build:    idf.py set-target esp32 && idf.py build
# Flash:    idf.py -p COMx flash monitor
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Make the in-tree components/ visible to the project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

# Pull in the ESP-IDF project() helper which defines the `app` target and
# everything else (newlib, freertos, lwip, esp_wifi, ...).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(SmartEVSE_v3)

# Generate compile_commands.json for IDEs (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
