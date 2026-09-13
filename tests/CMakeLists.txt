# Hardware Capability Registry - test targets.
# Copyright 2026 Summon Software Labs.
# SPDX-License-Identifier: Apache-2.0

set(HCR_CORE_TEST_SOURCES
  main.cpp
  test_core.cpp
  test_registry.cpp
  test_persistence.cpp
  test_protocol.cpp
  test_property.cpp
)

add_executable(hcr_tests ${HCR_CORE_TEST_SOURCES})
target_link_libraries(hcr_tests PRIVATE hcr)
target_include_directories(hcr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
hcr_apply_warnings(hcr_tests)
hcr_apply_definitions(hcr_tests)

add_executable(hcr_distributed_tests main.cpp test_distributed.cpp)
target_link_libraries(hcr_distributed_tests PRIVATE hcr)
target_include_directories(hcr_distributed_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
hcr_apply_warnings(hcr_distributed_tests)
hcr_apply_definitions(hcr_distributed_tests)
if(TARGET hcr_cli)
  target_compile_definitions(hcr_distributed_tests PRIVATE HCR_CLI_PATH="$<TARGET_FILE:hcr_cli>")
endif()

add_executable(hcr_hardware_tests main.cpp test_hardware.cpp)
target_link_libraries(hcr_hardware_tests PRIVATE hcr)
target_include_directories(hcr_hardware_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
hcr_apply_warnings(hcr_hardware_tests)
hcr_apply_definitions(hcr_hardware_tests)

enable_testing()
add_test(NAME hcr.core COMMAND hcr_tests)
add_test(NAME hcr.distributed COMMAND hcr_distributed_tests)
add_test(NAME hcr.hardware COMMAND hcr_hardware_tests)
