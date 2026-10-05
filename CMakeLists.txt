cmake_minimum_required(VERSION 3.16)
project(sensorhub VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

# Core library: sensors + ring buffer + acquisition engine (no FUSE dependency)
add_library(shub_core STATIC src/core/hub_core.cpp src/core/sensors.cpp)
target_include_directories(shub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shub_core PUBLIC Threads::Threads)

# User-space client
add_executable(shctl src/cli/shctl.cpp)
target_link_libraries(shctl PRIVATE shub_core)

# CUSE driver daemon (needs libfuse3)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3)
endif()
if(FUSE3_FOUND)
  add_executable(sensorhub-driver src/driver/main.cpp)
  target_link_libraries(sensorhub-driver PRIVATE shub_core PkgConfig::FUSE3)
  install(TARGETS sensorhub-driver RUNTIME DESTINATION sbin)
else()
  message(WARNING "libfuse3 not found: sensorhub-driver will NOT be built "
                  "(sudo apt install libfuse3-dev pkg-config)")
endif()
install(TARGETS shctl RUNTIME DESTINATION bin)

# Tests
enable_testing()
foreach(t test_ring_buffer test_sensors test_hub_core)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE shub_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
