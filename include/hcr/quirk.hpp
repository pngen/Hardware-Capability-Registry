// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
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

/// Quirk category. Quirks are first-class knowledge, not comments.
enum class QuirkCategory {
  CapabilityMisreport = 0,
  DriverDefect,
  FirmwareDefect,
  RevisionLimitation,
  RequiredWorkaround,
  BrokenFeatureCombination,
  PerformanceCaveat,
  ResetRequirement,
  UnsupportedCombination,
  VersionIncompatibility,
  Other,
};

const char* to_string(QuirkCategory value) noexcept;
bool quirk_category_from_string(std::string_view text, QuirkCategory& out) noexcept;

/// Severity drives deterministic quirk precedence.
enum class QuirkSeverity {
  Informational = 0,
  Caveat,
  Degraded,
  Unusable,
  Fatal,
};

const char* to_string(QuirkSeverity value) noexcept;
bool quirk_severity_from_string(std::string_view text, QuirkSeverity& out) noexcept;

/// Recorded mitigation. The registry records the requirement; it never
/// executes workaround scripts.
enum class MitigationKind {
  None = 0,
  UpgradeDriver,
  UpgradeFirmware,
  UpgradeRuntime,
  ChangeDeviceMode,
  ChangePlatformSetting,
  DisableFeature,
  UseAlternateCapability,
  ResetDevice,
  ContactVendor,
};

const char* to_string(MitigationKind value) noexcept;
bool mitigation_kind_from_string(std::string_view text, MitigationKind& out) noexcept;

/// Inclusive revision band. Revision tokens are compared lexicographically when
/// they are not numeric; the band is validated at publication time.
struct RevisionRange {
  std::string minimum;
  std::string maximum;
  friend bool operator==(const RevisionRange& a, const RevisionRange& b) noexcept {
    return a.minimum == b.minimum && a.maximum == b.maximum;
  }
};

/// Quirk applicability selector. Every field is optional; an unset selector
/// matches everything at that level. All set selectors must match for the
/// quirk to apply.
struct QuirkApplicability {
  std::vector<VendorId> vendors;
  std::vector<HardwareFamilyId> families;
  std::vector<HardwareModelId> models;
  std::vector<DeviceId> devices;
  std::vector<HardwareRevisionId> revisions;
  std::vector<RevisionRange> revision_ranges;
  std::vector<ArchitectureId> architectures;
  std::vector<VersionRange> driver_ranges;
  std::vector<VersionRange> firmware_ranges;
  std::vector<VersionRange> runtime_ranges;
  ConditionPtr platform_conditions;
};

Status validate_quirk_applicability(const QuirkApplicability& applicability, const RegistryLimits& limits);

/// A published quirk.
struct QuirkRecord {
  QuirkId id;
  QuirkGeneration quirk_generation;
  std::string title;
  std::string description;
  QuirkCategory category = QuirkCategory::Other;
  QuirkSeverity severity = QuirkSeverity::Informational;
  MitigationKind mitigation = MitigationKind::None;
  QuirkApplicability applicability;
  /// Capabilities whose truth this quirk modifies. Must be non-empty: a quirk
  /// that impacts nothing is not registry knowledge.
  std::vector<CapabilityId> impacted_capabilities;
  EvidenceSource source;
  bool withdrawn = false;
};

Status validate_quirk_record(const QuirkRecord& record, const RegistryLimits& limits);

/// Deterministic quirk ordering key: higher severity first, then by quirk id,
/// then by quirk generation. Used for quirk precedence and stable listing.
bool quirk_precedes(const QuirkRecord& a, const QuirkRecord& b) noexcept;

/// Outcome of evaluating one quirk against one device.
enum class QuirkApplicabilityResult {
  Applies = 0,
  DoesNotApply,
  Unknown,
};

const char* to_string(QuirkApplicabilityResult value) noexcept;

/// One quirk evaluated against a device/software state.
struct QuirkApplication {
  QuirkId id;
  QuirkGeneration quirk_generation;
  QuirkSeverity severity = QuirkSeverity::Informational;
  MitigationKind mitigation = MitigationKind::None;
  QuirkApplicabilityResult result = QuirkApplicabilityResult::DoesNotApply;
  std::string reason;
  std::string title;
};

/// Evaluates quirk applicability against a device, its revision/architecture
/// and its software state. Never mutates anything; deterministic for identical
/// inputs.
QuirkApplication evaluate_quirk(const QuirkRecord& quirk,
                                const DeviceIdentity& device,
                                const DeviceSoftwareState& software,
                                const EnvironmentContext& environment);

}  // namespace hcr
