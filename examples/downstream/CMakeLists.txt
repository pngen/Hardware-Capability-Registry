# Independent downstream consumer.
#
# This project is deliberately NOT part of the registry build: it consumes only
# installed artifacts through find_package, exactly as a third-party system
# would.
cmake_minimum_required(VERSION 3.20)

project(HcrDownstreamConsumer
  VERSION 1.0.0
  DESCRIPTION "Independent consumer of the installed Hardware Capability Registry"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(HardwareCapabilityRegistry CONFIG REQUIRED)

add_executable(hcr_downstream_consumer main.cpp)
target_link_libraries(hcr_downstream_consumer PRIVATE SummonSoftwareLabs::HardwareCapabilityRegistry)

if(MSVC)
  target_compile_options(hcr_downstream_consumer PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc /utf-8)
  target_compile_definitions(hcr_downstream_consumer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(hcr_downstream_consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

message(STATUS "Hardware Capability Registry found at ${HardwareCapabilityRegistry_DIR}")
