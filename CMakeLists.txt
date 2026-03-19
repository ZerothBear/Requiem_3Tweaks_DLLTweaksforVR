cmake_minimum_required(VERSION 3.31)

set(VCPKG_DISABLE_METRICS ON)
set(VCPKG_INSTALL_OPTIONS "--no-print-usage")

project(
        DLLTweaksVR
        VERSION 1.0.0
        LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Build options
option(AUTO_PLUGIN_DEPLOYMENT "Copy the build output to env:AUTO_PLUGIN_DEPLOYMENT_DIRS." OFF)
option(ZIP_TO_DIST "Zip the build output to their own 7z file in .\\dist." OFF)

include(XSEPlugin)
include(Packaging)
