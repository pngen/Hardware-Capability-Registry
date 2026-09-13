// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/condition.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/limits.hpp"
#include "hcr/provenance.hpp"
#include "hcr/software_version.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Hardware-centered compatibility relationships. Generalized software
/// compatibility management belongs to a compatibility registry, not here.
enum class CompatibilitySubjectKind {
  AcceleratorRuntime = 0,
  AcceleratorDriver,
  AcceleratorCompilerTarget,
  AcceleratorKernelArchitecture,
  NicRdmaStack,
  GpuNicDirectPath,
  CxlPlatform,
  PartitionProfileDevice,
  FirmwareDriver,
  DpuServiceHardware,
  MemoryTypeExecutionEngine,
  PlatformDriver,
  Other,
};

const char* to_string(CompatibilitySubjectKind value) noexcept;
bool compatibility_subject_kind_from_string(std::string_view text, CompatibilitySubjectKind& out) noexcept;

/// A reference to one side of a compatibility fact. Device, family, model and
/// architecture references must resolve against registered subjects;
/// software references are structural identities validated for canonical form.
struct HardwareReference {
  enum class Kind {
    Device = 0,
    HardwareFamily,
    HardwareModel,
    Architecture,
    Vendor,
    Firmware,
    Driver,
    Runtime,
    Platform,
  };

  Kind kind = Kind::Device;
  /// Canonical token interpreted according to kind.
  std::string token;
  /// Optional version qualifier (firmware, driver, runtime).
  SoftwareVersion version;

  friend bool operator==(const HardwareReference& a, const HardwareReference& b) noexcept {
    return a.kind == b.kind && a.token == b.token &&
           SoftwareVersion::compare(a.version, b.version) == 0;
  }
  friend bool operator<(const HardwareReference& a, const HardwareReference& b) noexcept;
};

const char* to_string(HardwareReference::Kind value) noexcept;
bool hardware_reference_kind_from_string(std::string_view text, HardwareReference::Kind& out) noexcept;

/// True when the reference must resolve against a registered subject.
bool hardware_reference_requires_subject(HardwareReference::Kind kind) noexcept;

enum class CompatibilityOutcome {
  Compatible = 0,
  CompatibleConditional,
  Incompatible,
  Unknown,
  Unsupported,
  RevalidationRequired,
};

const char* to_string(CompatibilityOutcome value) noexcept;
bool compatibility_outcome_from_string(std::string_view text, CompatibilityOutcome& out) noexcept;

struct CompatibilityFact {
  CompatibilityFactId id;
  CompatibilityGeneration compatibility_generation;
  CompatibilitySubjectKind kind = CompatibilitySubjectKind::Other;
  HardwareReference lhs;
  HardwareReference rhs;
  CompatibilityOutcome outcome = CompatibilityOutcome::Unknown;
  /// Required versions/conditions when the outcome is conditional.
  ConditionPtr conditions;
  VersionRange version_window;
  /// Exact reason, always non-empty for Incompatible and Unsupported.
  std::string reason;
  EvidenceSource source;
  bool withdrawn = false;
};

Status validate_compatibility_fact(const CompatibilityFact& fact, const RegistryLimits& limits);

}  // namespace hcr
