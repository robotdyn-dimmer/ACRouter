# AC Power Router Controller
# ESP-IDF Project with Arduino Core 3.x

cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project definition
project(ACRouter-project VERSION 1.2.0)

# Build information
message(STATUS "Building AC Power Router Controller v${PROJECT_VERSION}")
message(STATUS "ESP-IDF Version: $ENV{IDF_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")