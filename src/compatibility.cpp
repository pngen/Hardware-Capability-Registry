// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/compatibility.hpp"

#include "hcr/canonical.hpp"

namespace hcr {

const char* to_string(CompatibilitySubjectKind value) noexcept {
  switch (value) {
    case CompatibilitySubjectKind::AcceleratorRuntime: return "accelerator-runtime";
    case CompatibilitySubjectKind::AcceleratorDriver: return "accelerator-driver";
    case CompatibilitySubjectKind::AcceleratorCompilerTarget: return "accelerator-compiler-target";
    case CompatibilitySubjectKind::AcceleratorKernelArchitecture: return "accelerator-kernel-architecture";
    case CompatibilitySubjectKind::NicRdmaStack: return "nic-rdma-stack";
    case CompatibilitySubjectKind::GpuNicDirectPath: return "gpu-nic-direct-path";
    case CompatibilitySubjectKind::CxlPlatform: return "cxl-platform";
    case CompatibilitySubjectKind::PartitionProfileDevice: return "partition-profile-device";
    case CompatibilitySubjectKind::FirmwareDriver: return "firmware-driver";
    case CompatibilitySubjectKind::DpuServiceHardware: return "dpu-service-hardware";
    case CompatibilitySubjectKind::MemoryTypeExecutionEngine: return "memory-type-execution-engine";
    case CompatibilitySubjectKind::PlatformDriver: return "platform-driver";
    case CompatibilitySubjectKind::Other: return "other";
  }
  return "other";
}

bool compatibility_subject_kind_from_string(std::string_view text, CompatibilitySubjectKind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(CompatibilitySubjectKind::Other); ++i) {
    const auto value = static_cast<CompatibilitySubjectKind>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

const char* to_string(HardwareReference::Kind value) noexcept {
  switch (value) {
    case HardwareReference::Kind::Device: return "device";
    case HardwareReference::Kind::HardwareFamily: return "hardware-family";
    case HardwareReference::Kind::HardwareModel: return "hardware-model";
    case HardwareReference::Kind::Architecture: return "architecture";
    case HardwareReference::Kind::Vendor: return "vendor";
    case HardwareReference::Kind::Firmware: return "firmware";
    case HardwareReference::Kind::Driver: return "driver";
    case HardwareReference::Kind::Runtime: return "runtime";
    case HardwareReference::Kind::Platform: return "platform";
  }
  return "device";
}

bool hardware_reference_kind_from_string(std::string_view text, HardwareReference::Kind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(HardwareReference::Kind::Platform); ++i) {
    const auto value = static_cast<HardwareReference::Kind>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

bool hardware_reference_requires_subject(HardwareReference::Kind kind) noexcept {
  switch (kind) {
    case HardwareReference::Kind::Device:
    case HardwareReference::Kind::HardwareFamily:
    case HardwareReference::Kind::HardwareModel:
    case HardwareReference::Kind::Architecture:
    case HardwareReference::Kind::Platform:
      return true;
    default:
      return false;
  }
}

bool operator<(const HardwareReference& a, const HardwareReference& b) noexcept {
  if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
  if (a.token != b.token) return a.token < b.token;
  return SoftwareVersion::compare(a.version, b.version) < 0;
}

const char* to_string(CompatibilityOutcome value) noexcept {
  switch (value) {
    case CompatibilityOutcome::Compatible: return "COMPATIBLE";
    case CompatibilityOutcome::CompatibleConditional: return "COMPATIBLE_CONDITIONAL";
    case CompatibilityOutcome::Incompatible: return "INCOMPATIBLE";
    case CompatibilityOutcome::Unknown: return "UNKNOWN";
    case CompatibilityOutcome::Unsupported: return "UNSUPPORTED";
    case CompatibilityOutcome::RevalidationRequired: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}

bool compatibility_outcome_from_string(std::string_view text, CompatibilityOutcome& out) noexcept {
  for (int i = 0; i <= static_cast<int>(CompatibilityOutcome::RevalidationRequired); ++i) {
    const auto value = static_cast<CompatibilityOutcome>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

Status validate_compatibility_fact(const CompatibilityFact& fact, const RegistryLimits& limits) {
  if (!fact.id.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "compatibility fact requires a canonical identity");
  }
  if (!fact.compatibility_generation.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "compatibility fact requires a generation");
  }
  if (!is_canonical_token(fact.lhs.token) || !is_canonical_token(fact.rhs.token)) {
    return Status::failure(ErrorCode::InvalidReference, "compatibility reference token is not canonical");
  }
  if (fact.lhs.kind == fact.rhs.kind && fact.lhs.token == fact.rhs.token &&
      SoftwareVersion::compare(fact.lhs.version, fact.rhs.version) == 0) {
    return Status::failure(ErrorCode::InvalidReference, "compatibility fact references the same subject twice");
  }
  if (!fact.reason.empty() && fact.reason.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "compatibility reason exceeds length limit");
  }
  if ((fact.outcome == CompatibilityOutcome::Incompatible || fact.outcome == CompatibilityOutcome::Unsupported) &&
      fact.reason.empty()) {
    return Status::failure(ErrorCode::InvalidArgument,
                           "incompatible or unsupported compatibility facts require a reason");
  }
  if (!fact.version_window.valid()) {
    return Status::failure(ErrorCode::InvalidRange, "compatibility version window is inverted");
  }
  if (!fact.source.publisher.id.valid()) {
    return Status::failure(ErrorCode::InvalidProvenance, "compatibility fact requires a publisher identity");
  }
  if (fact.source.adapter.empty() || fact.source.adapter.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::InvalidProvenance, "compatibility fact requires a bounded source adapter");
  }
  if (fact.source.source_class == SourceClass::Unspecified) {
    return Status::failure(ErrorCode::InvalidProvenance, "compatibility fact requires a source class");
  }
  if (!provenance_allowed_for(fact.source.provenance, fact.source.source_class)) {
    return Status::failure(ErrorCode::InvalidProvenance,
                           "compatibility provenance is stronger than its source class can support");
  }
  if (fact.conditions != nullptr) {
    return validate_condition(*fact.conditions, limits);
  }
  return Status::success();
}

}  // namespace hcr
