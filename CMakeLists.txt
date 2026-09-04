cmake_minimum_required(VERSION 3.22)

project(PreemptionFabric
  VERSION 1.0.0
  DESCRIPTION "Open-source, vendor-neutral C++20 runtime for governing safe preemption"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PF_BUILD_TESTS "Build Preemption Fabric tests" ON)
option(PF_BUILD_EXAMPLES "Build Preemption Fabric examples" ON)
option(PF_BUILD_BENCHMARKS "Build Preemption Fabric benchmarks" ON)
option(PF_BUILD_TOOLS "Build Preemption Fabric inspection CLI" ON)
option(PF_BUILD_CUDA "Build CUDA physical proof (if a CUDA toolkit is available)" ON)
option(PF_BUILD_ASAN "Build with AddressSanitizer (supported compilers)" OFF)
option(PF_ENABLE_STRICT_WARNINGS "Enable strict warnings and warnings-as-errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Strict warnings
# ---------------------------------------------------------------------------
if(PF_ENABLE_STRICT_WARNINGS)
  if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
  else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
endif()

if(PF_BUILD_ASAN)
  if(MSVC)
    # /Zi: MSVC ASan needs debug info for meaningful error reporting.
    add_compile_options(/fsanitize=address /Zi /Ob0)
    add_compile_options(/O1)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  endif()
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# The core library (public API + coordination reference implementation)
# ---------------------------------------------------------------------------
add_library(preemption_fabric
  src/preemption_fabric.cpp
  src/persistence/store.cpp
  src/protocol/framing.cpp
  src/protocol/connection.cpp
  src/runtime.cpp
)
add_library(PreemptionFabric::PreemptionFabric ALIAS preemption_fabric)

target_include_directories(preemption_fabric
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(preemption_fabric PUBLIC Threads::Threads)

set_target_properties(preemption_fabric PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME PreemptionFabric)

# ---------------------------------------------------------------------------
# Install + export
# ---------------------------------------------------------------------------
install(TARGETS preemption_fabric
  EXPORT PreemptionFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/preemption_fabric
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT PreemptionFabricTargets
  FILE PreemptionFabricTargets.cmake
  NAMESPACE PreemptionFabric::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PreemptionFabric)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreemptionFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/PreemptionFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PreemptionFabric)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/PreemptionFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/PreemptionFabricConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/PreemptionFabricConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PreemptionFabric)

# ---------------------------------------------------------------------------
# Reference multiprocess coordinator + worker executables
# ---------------------------------------------------------------------------
add_executable(pf_coordinator src/coordinator/coordinator.cpp)
target_link_libraries(pf_coordinator PRIVATE PreemptionFabric::PreemptionFabric)

add_executable(pf_worker src/coordinator/worker.cpp)
target_link_libraries(pf_worker PRIVATE PreemptionFabric::PreemptionFabric)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
if(PF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(PF_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(PF_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(PF_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(PF_BUILD_CUDA AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cuda/CMakeLists.txt")
  add_subdirectory(cuda)
endif()
