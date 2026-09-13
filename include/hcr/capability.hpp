// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "hcr/condition.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/limits.hpp"
#include "hcr/provenance.hpp"
#include "hcr/status.hpp"
#include "hcr/support.hpp"
#include "hcr/value.hpp"

namespace hcr {

/// Capability families. Class-specific namespaces live inside these domains
/// rather than in one flat schema.
enum class CapabilityDomain {
  Other = 0,
  Compute,
  Memory,
  Partitioning,
  Interconnect,
  Networking,
  Runtime,
  Reliability,
  Power,
  Platform,
};

const char* to_string(CapabilityDomain value) noexcept;
bool capability_domain_from_string(std::string_view text, CapabilityDomain& out) noexcept;

/// A typed, versioned capability schema. The version is embedded in the
/// CapabilitySchemaId token ("...v<N>"); capabilities are never published
/// against an unknown schema, which keeps the registry free of untyped
/// string-to-string capability bags.
struct CapabilitySchema {
  CapabilitySchemaId id;
  CapabilityId capability;
  CapabilityDomain domain = CapabilityDomain::Other;
  ValueKind value_kind = ValueKind::None;
  std::string canonical_name;
  std::string unit;
};

Status validate_capability_schema(const CapabilitySchema& schema, const RegistryLimits& limits);

/// One published capability observation for one device generation.
///
/// A record is immutable once applied. Superseding evidence never mutates an
/// existing record; it appends a new record and marks the older one historical.
struct CapabilityRecord {
  DeviceId device;
  DeviceGeneration device_generation;
  DeviceBootId device_boot;
  CapabilityId capability;
  CapabilitySchemaId schema;
  /// Generation of this capability for this device. Advances on every accepted
  /// change to the capability's current truth and never regresses.
  CapabilityGeneration capability_generation;
  SupportState support = SupportState::Unknown;
  CapabilityValue value;
  PrecisionClass precision = PrecisionClass::Unknown;
  EvidenceSource source;
  /// Conditions under which the declared support holds. Null means
  /// unconditional.
  ConditionPtr conditions;
  FirmwareGeneration firmware_generation;
  DriverGeneration driver_generation;
  RuntimeGeneration runtime_generation;
  PlatformGeneration platform_generation;
  /// Component identities in force when the observation was made. A record is
  /// qualified by the exact driver, firmware and runtime it was observed under,
  /// so an axis change can invalidate precisely the evidence it affects.
  DriverId driver_id;
  FirmwareId firmware_id;
  RuntimeId runtime_id;
  /// True for an explicit withdrawal publication: positive evidence that the
  /// capability must no longer be considered current.
  bool withdrawn = false;
};

Status validate_capability_record(const CapabilityRecord& record, const RegistryLimits& limits);

}  // namespace hcr
