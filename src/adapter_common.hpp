// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Shared helpers for discovery adapters. Internal to the implementation.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hcr/canonical.hpp"
#include "hcr/capability.hpp"
#include "hcr/discovery.hpp"

namespace hcr::adapter_detail {

inline CapabilityId cap(std::string_view name) { return CapabilityId::parse(name); }
inline CapabilitySchemaId schema(std::string_view name) { return CapabilitySchemaId::parse(name); }

/// Canonical token derived from a human- or vendor-supplied string: lowercased,
/// non-canonical characters replaced by '.', collapsed and length-bounded. The
/// original string is preserved separately in provenance metadata.
std::string canonical_token(std::string_view text);

/// Short stable token derived from an arbitrary identifier.
std::string token_from_hash(std::string_view prefix, std::string_view text);

/// Lowercase hex encoding. Used for UUID and hash renderings.
std::string hex_bytes(const void* data, std::size_t size);

/// Formats a 16-byte CUDA/NVML device UUID the same way the vendor tooling
/// does ("GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx") so that independent
/// adapters derive exactly one device identity.
std::string format_gpu_uuid(const std::uint8_t* bytes);

inline CountValue count(std::uint64_t value, std::string unit = {}) {
  CountValue result;
  result.count = value;
  result.unit = std::move(unit);
  return result;
}

inline BytesValue bytes(std::uint64_t value) {
  BytesValue result;
  result.bytes = value;
  return result;
}

inline EnumValue enumeration(std::string domain, std::string value) {
  EnumValue result;
  result.domain = std::move(domain);
  result.value = std::move(value);
  return result;
}

inline FeatureSetValue features(std::vector<std::string> values) {
  FeatureSetValue result;
  result.features = std::move(values);
  return result;
}

inline BandwidthValue bits_per_second(std::uint64_t magnitude) {
  BandwidthValue result;
  result.unit = BandwidthUnit::BitsPerSecond;
  if (magnitude >= 1000000000ull && magnitude % 1000000000ull == 0) {
    result.magnitude = magnitude / 1000000000ull;
    result.unit = BandwidthUnit::GigabitsPerSecond;
  } else if (magnitude >= 1000000ull && magnitude % 1000000ull == 0) {
    result.magnitude = magnitude / 1000000ull;
    result.unit = BandwidthUnit::MegabitsPerSecond;
  } else if (magnitude >= 1000ull && magnitude % 1000ull == 0) {
    result.magnitude = magnitude / 1000ull;
    result.unit = BandwidthUnit::KilobitsPerSecond;
  } else {
    result.magnitude = magnitude;
  }
  return result;
}

/// A convenience condition builder for leaf predicates.
ConditionPtr condition(ConditionKind kind, std::string key, std::string value);

/// A convenience builder for "all of" conditions.
ConditionPtr all_of(std::vector<ConditionPtr> children);

}  // namespace hcr::adapter_detail
