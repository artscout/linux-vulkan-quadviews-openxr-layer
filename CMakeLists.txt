cmake_minimum_required(VERSION 3.22)
project(qv-vulkan VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(QV_BUILD_GUI "Build the Qt settings app" ON)
option(QV_BUILD_TESTS "Build the unit tests" ON)

include(GNUInstallDirs)

# The layer's name, as the OpenXR loader sees it, and where the library goes.
set(QV_LAYER_NAME "XR_APILAYER_ARTSCOUT_quad_views_vulkan")
set(QV_LAYER_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}/qv-vulkan")
# Implicit layers are found in <data dir>/openxr/1/api_layers/implicit.d (XDG_DATA_HOME or XDG_DATA_DIRS).
set(QV_MANIFEST_DIR "${CMAKE_INSTALL_FULL_DATADIR}/openxr/1/api_layers/implicit.d")

find_package(Vulkan REQUIRED)
# Only the OpenXR headers are needed: a layer never links the loader.
find_path(OPENXR_INCLUDE_DIR openxr/openxr_loader_negotiation.h REQUIRED)
add_library(qv_openxr_headers INTERFACE)
target_include_directories(qv_openxr_headers INTERFACE ${OPENXR_INCLUDE_DIR})
target_compile_definitions(qv_openxr_headers INTERFACE XR_USE_GRAPHICS_API_VULKAN)

add_compile_options(-Wall -Wextra -Wno-missing-field-initializers)

add_subdirectory(layer)
if(QV_BUILD_GUI)
  add_subdirectory(gui)
endif()
if(QV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
