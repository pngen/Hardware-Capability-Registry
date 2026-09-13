// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/capability.hpp"

#include "hcr/canonical.hpp"

namespace hcr {

const char* to_string(CapabilityDomain value) noexcept {
  switch (value) {
    case CapabilityDomain::Other: return "other";
    case CapabilityDomain::Compute: return "compute";
    case CapabilityDomain::Memory: return "memory";
    case CapabilityDomain::Partitioning: return "partitioning";
    case CapabilityDomain::Interconnect: return "interconnect";
    case CapabilityDomain::Networking: return "networking";
    case CapabilityDomain::Runtime: return "runtime";
    case CapabilityDomain::Reliability: return "reliability";
    case CapabilityDomain::Power: return "power";
    case CapabilityDomain::Platform: return "platform";
  }
  return "other";
}

bool capability_domain_from_string(std::string_view text, CapabilityDomain& out) noexcept {
  for (int i = 0; i <= static_cast<int>(CapabilityDomain::Platform); ++i) {
    const auto value = static_cast<CapabilityDomain>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

Status validate_capability_schema(const CapabilitySchema& schema, const RegistryLimits& limits) {
  if (!schema.id.valid()) {
    return Status::failure(ErrorCode::InvalidCapability, "schema identity is not canonical");
  }
  if (!schema.capability.valid()) {
    return Status::failure(ErrorCode::InvalidCapability, "capability identity is not canonical");
  }
  if (schema.value_kind == ValueKind::None) {
    return Status::failure(ErrorCode::InvalidCapability, "schema must declare a typed value kind");
  }
  if (schema.canonical_name.empty() || schema.canonical_name.size() > limits.max_name_length) {
    return Status::failure(ErrorCode::InvalidCapability, "schema requires a bounded canonical name");
  }
  if (!schema.unit.empty() && schema.unit.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "schema unit exceeds length limit");
  }
  // The schema token must carry its version so that persisted and wire data can
  // never silently reinterpret a capability against a different schema.
  const std::string& token = schema.id.str();
  const std::size_t marker = token.rfind(".v");
  if (marker == std::string::npos || marker + 2 >= token.size()) {
    return Status::failure(ErrorCode::InvalidCapability, "schema identity must embed a version suffix");
  }
  for (std::size_t i = marker + 2; i < token.size(); ++i) {
    const char c = token[i];
    if (c < '0' || c > '9') {
      return Status::failure(ErrorCode::InvalidCapability, "schema version suffix must be numeric");
    }
  }
  return Status::success();
}

Status validate_capability_record(const CapabilityRecord& record, const RegistryLimits& limits) {
  if (!record.device.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "capability record requires a device identity");
  }
  if (!record.device_generation.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "capability record requires a device generation");
  }
  if (!record.capability.valid()) {
    return Status::failure(ErrorCode::InvalidCapability, "capability record requires a capability identity");
  }
  if (!record.schema.valid()) {
    return Status::failure(ErrorCode::InvalidCapability, "capability record requires a schema identity");
  }
  if (!record.source.publisher.id.valid()) {
    return Status::failure(ErrorCode::InvalidProvenance, "capability record requires a publisher identity");
  }
  if (record.source.adapter.empty()) {
    return Status::failure(ErrorCode::InvalidProvenance, "capability record requires a source adapter");
  }
  if (record.source.adapter.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "source adapter exceeds length limit");
  }
  if (record.source.native_reference.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "native reference exceeds length limit");
  }
  if (record.source.observed_at_utc.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "observation timestamp exceeds length limit");
  }
  if (record.source.source_class == SourceClass::Unspecified) {
    return Status::failure(ErrorCode::InvalidProvenance, "capability record requires a source class");
  }
  if (!provenance_allowed_for(record.source.provenance, record.source.source_class)) {
    return Status::failure(ErrorCode::InvalidProvenance,
                           "claimed provenance is stronger than the source class can support");
  }
  if (record.withdrawn) {
    if (record.support != SupportState::Unknown) {
      return Status::failure(ErrorCode::InvalidSupportState,
                             "a withdrawn capability must not assert a support state");
    }
    if (!record.value.empty()) {
      return Status::failure(ErrorCode::InvalidValue, "a withdrawn capability must not carry a value");
    }
  }
  if (record.conditions != nullptr) {
    const Status status = validate_condition(*record.conditions, limits);
    if (!status.ok()) return status;
  }
  return validate_value(record.value, limits);
}

}  // namespace hcr
