// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace hcr {

/// Every resource the registry retains is bounded. The defaults are the values
/// used in production builds; tests raise or lower them explicitly to exercise
/// backpressure and limit rejection deterministically.
struct RegistryLimits {
  std::size_t max_devices = 20000;
  std::size_t max_families = 8192;
  std::size_t max_capabilities_per_device = 2048;
  std::size_t max_evidence_per_capability = 8;
  std::size_t max_evidence_history = 262144;
  std::size_t max_quirks = 16384;
  std::size_t max_compatibility_facts = 16384;
  std::size_t max_publishers = 512;
  std::size_t max_boot_records_per_publisher = 64;
  std::size_t max_metadata_bytes = 512;
  std::size_t max_native_identifiers = 24;
  std::size_t max_name_length = 192;
  std::size_t max_record_depth = 4;
  std::size_t max_record_fields = 32;
  std::size_t max_feature_set_entries = 256;
  std::size_t max_architecture_set_entries = 64;
  std::size_t max_condition_nodes = 64;
  std::size_t max_condition_children = 32;
  std::size_t max_query_results = 8192;
  std::size_t max_comparison_entries = 8192;
  std::size_t max_snapshot_records = 500000;
  std::size_t max_frame_bytes = 1u << 20;
  std::size_t max_persistence_bytes = 256u << 20;
  std::size_t max_connections = 64;
  std::size_t max_pending_publications = 1024;
  std::size_t max_pending_bytes = 8u << 20;
  std::size_t max_string_bytes = 4096;
};

}  // namespace hcr
