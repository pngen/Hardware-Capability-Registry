# Hardware Capability Registry - runnable examples.
# Copyright 2026 Summon Software Labs.
# SPDX-License-Identifier: Apache-2.0
#
# Every example uses only the public API and is installed as documentation of
# how a downstream system is expected to consume the registry.

set(HCR_EXAMPLES
  ex01_register_and_query
  ex02_real_capability_publication
  ex03_conditional_capability
  ex04_unsupported_capability
  ex05_device_reset_and_stale_evidence
  ex06_quirk_application
  ex07_contradictory_evidence
  ex08_hardware_comparison
  ex09_publisher_fencing
  ex10_persistence_and_recovery
  ex11_synthetic_fleet
)

foreach(example ${HCR_EXAMPLES})
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE hcr)
  target_include_directories(${example} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  hcr_apply_warnings(${example})
  hcr_apply_definitions(${example})
endforeach()
