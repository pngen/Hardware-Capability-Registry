cmake_minimum_required(VERSION 3.20)

project(HardwareCapabilityRegistry
  VERSION 1.0.0
  DESCRIPTION "Canonical, versioned, provenance-preserving hardware capability knowledge"
  HOMEPAGE_URL "https://github.com/pngen/Hardware-Capability-Registry"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(HCR_BUILD_SHARED "Build the registry as a shared library" OFF)
option(HCR_BUILD_TESTS "Build the test suites" ON)
option(HCR_BUILD_EXAMPLES "Build the examples" ON)
option(HCR_BUILD_BENCHMARKS "Build the benchmark suite" ON)
option(HCR_BUILD_CLI "Build the hcr command line tool" ON)
option(HCR_BUILD_CUDA_VALIDATION "Build the CUDA hardware validation program when a toolkit is present" ON)
option(HCR_ENABLE_ASAN "Build with AddressSanitizer (MSVC /fsanitize=address)" OFF)
option(HCR_WARNINGS_AS_ERRORS "Treat first-party warnings as errors" ON)

set(HCR_LIBRARY_TYPE STATIC)
if(HCR_BUILD_SHARED)
  set(HCR_LIBRARY_TYPE SHARED)
endif()

set(HCR_SOURCES
  src/status.cpp
  src/ids.cpp
  src/software_version.cpp
  src/condition.cpp
  src/value.cpp
  src/support.cpp
  src/provenance.cpp
  src/hardware.cpp
  src/capability.cpp
  src/quirk.cpp
  src/compatibility.cpp
  src/catalog.cpp
  src/time.cpp
  src/canonical.cpp
  src/registry.cpp
  src/render.cpp
  src/persistence.cpp
  src/transport.cpp
  src/protocol.cpp
  src/coordinator.cpp
  src/publisher.cpp
  src/client.cpp
  src/discovery.cpp
  src/adapter_cpu.cpp
  src/adapter_pci.cpp
  src/adapter_nvml.cpp
  src/adapter_cuda.cpp
  src/adapter_network.cpp
  src/adapter_cxl.cpp
  src/adapter_synthetic.cpp
  src/adapter_common.cpp
  src/codec.cpp
)

add_library(hcr ${HCR_LIBRARY_TYPE} ${HCR_SOURCES})

add_library(SummonSoftwareLabs::HardwareCapabilityRegistry ALIAS hcr)
set_target_properties(hcr PROPERTIES
  OUTPUT_NAME HardwareCapabilityRegistry
  EXPORT_NAME HardwareCapabilityRegistry
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(hcr
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Every first-party target shares the same strict warning set and platform
# definitions; no target may opt out silently.
function(hcr_apply_definitions target)
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(WIN32)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  endif()
endfunction()

function(hcr_apply_warnings target)
  if(MSVC)
    # Options are guarded by language: host-compiler switches must never reach
    # nvcc as bare arguments, where they would be mistaken for input files.
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/Zc:__cplusplus;/EHsc;/utf-8;/bigobj>
      $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4,-Xcompiler=/EHsc,-Xcompiler=/utf-8>)
    if(HCR_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion>)
    if(HCR_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
  endif()
  if(HCR_ENABLE_ASAN)
    if(MSVC)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address;/Zi>)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    else()
      target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
      target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
  endif()
endfunction()

hcr_apply_warnings(hcr)
hcr_apply_definitions(hcr)
target_compile_definitions(hcr PRIVATE HCR_BUILDING_LIBRARY=1)

find_package(Threads REQUIRED)
target_link_libraries(hcr PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(hcr PRIVATE ws2_32 iphlpapi setupapi cfgmgr32)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS hcr
  EXPORT HardwareCapabilityRegistryTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT HardwareCapabilityRegistryTargets
  FILE HardwareCapabilityRegistryTargets.cmake
  NAMESPACE SummonSoftwareLabs::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardwareCapabilityRegistry)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HardwareCapabilityRegistryConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/HardwareCapabilityRegistryConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardwareCapabilityRegistry)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/HardwareCapabilityRegistryConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/HardwareCapabilityRegistryConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/HardwareCapabilityRegistryConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardwareCapabilityRegistry)

install(FILES LICENSE README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/HardwareCapabilityRegistry
  OPTIONAL)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(HCR_BUILD_CLI)
  # The command line tool is a separate target from the library so that both
  # can be named after the product on disk.
  add_executable(hcr_cli src/cli_main.cpp)
  set_target_properties(hcr_cli PROPERTIES OUTPUT_NAME hcr)
  target_link_libraries(hcr_cli PRIVATE hcr)
  hcr_apply_warnings(hcr_cli)
  hcr_apply_definitions(hcr_cli)
  install(TARGETS hcr_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(HCR_BUILD_CUDA_VALIDATION)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    # Build for the architecture actually present. The cache entry is forced so
    # that a stale default from an earlier configure cannot silently produce a
    # binary that cannot run on this machine; set HCR_CUDA_ARCHITECTURES to
    # override.
    if(HCR_CUDA_ARCHITECTURES)
      set(CMAKE_CUDA_ARCHITECTURES "${HCR_CUDA_ARCHITECTURES}" CACHE STRING "CUDA architectures" FORCE)
    else()
      set(CMAKE_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA architectures" FORCE)
    endif()
    # nvcc cannot emit a PDB path the way the MSVC host driver expects under
    # Ninja, so CUDA translation units use the host debug information settings
    # without the split PDB options.
    if(MSVC)
      string(REPLACE "/Zi" "" CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG}")
      string(REPLACE "/RTC1" "" CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG}")
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      set(CMAKE_CUDA_ARCHITECTURES native)
    endif()
    add_executable(hcr_cuda_validation src/cuda_validation.cu)
    target_link_libraries(hcr_cuda_validation PRIVATE hcr CUDA::cudart CUDA::cuda_driver)
    set_target_properties(hcr_cuda_validation PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON
      CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
    if(MSVC)
      # nvcc cannot be handed the split PDB options CMake derives for a Ninja
      # build when the compiler working directory ends in a separator, so the
      # CUDA translation unit emits its program database at an explicit
      # absolute path instead.
      set_target_properties(hcr_cuda_validation PROPERTIES
        MSVC_DEBUG_INFORMATION_FORMAT ""
        COMPILE_PDB_NAME "hcr_cuda_validation"
        COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
    if(MSVC)
      target_compile_options(hcr_cuda_validation PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc;/utf-8>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4,-Xcompiler=/EHsc,-Xcompiler=/utf-8>)
      if(HCR_WARNINGS_AS_ERRORS)
        target_compile_options(hcr_cuda_validation PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
      endif()
    endif()
  else()
    message(STATUS "CUDA toolkit not found: skipping hcr_cuda_validation")
  endif()
endif()

if(HCR_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
if(HCR_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory(examples)
endif()
if(HCR_BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
  add_subdirectory(benchmarks)
endif()
