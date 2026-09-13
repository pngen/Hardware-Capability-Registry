# Hardware Capability Registry - benchmarks.
# Copyright 2026 Summon Software Labs.
# SPDX-License-Identifier: Apache-2.0

add_executable(hcr_benchmarks bench_main.cpp)
target_link_libraries(hcr_benchmarks PRIVATE hcr)
hcr_apply_warnings(hcr_benchmarks)
hcr_apply_definitions(hcr_benchmarks)
