// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/condition.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/provenance.hpp"
#include "hcr/quirk.hpp"
#include "hcr/support.hpp"

namespace hcr {

/// One evidence record as seen by a resolved answer. Losing and stale evidence
/// is retained here for audit; it is never silently discarded.
struct EvidenceView {
  CapabilityGeneration capability_generation;
  SupportState support = SupportState::Unknown;
  CapabilityValue value;
  PrecisionClass precision = PrecisionClass::Unknown;
  EvidenceSource source;
  StalenessFlag staleness = StalenessFlag::None;
  bool winner = false;
  bool withdrawn = false;
};

/// The registry's answer for one (device, capability) pair.
struct ResolvedCapability {
  DeviceId device;
  DeviceGeneration device_generation;
  DeviceBootId device_boot;
  CapabilityId capability;
  CapabilitySchemaId schema;
  CapabilityGeneration capability_generation;
  /// State asserted by the winning evidence, before conditions and quirks.
  SupportState declared_support = SupportState::Unknown;
  /// State after conditions, quirks and staleness. Both are always reported so
  /// that "advertised but unusable here" never collapses into one value.
  EffectiveSupport effective_support = EffectiveSupport::Unknown;
  CapabilityValue value;
  PrecisionClass precision = PrecisionClass::Unknown;
  TruthClass truth = TruthClass::Unknown;
  ContradictionState contradiction = ContradictionState::Consistent;
  StalenessFlag staleness = StalenessFlag::None;
  ConditionPtr conditions;
  ConditionEvaluation condition_evaluation;
  /// Applying quirks, in deterministic precedence order, followed by quirks
  /// that were evaluated but do not apply (result DoesNotApply/Unknown).
  std::vector<QuirkApplication> quirks;
  /// All retained evidence for this key, in deterministic order.
  std::vector<EvidenceView> evidence;
  FirmwareGeneration firmware_generation;
  DriverGeneration driver_generation;
  RuntimeGeneration runtime_generation;
  /// True when this answer reflects the device's current generation.
  bool current = false;
  /// True when the capability key exists in the registry at all.
  bool known = false;
  /// Deterministic, human-readable explanation.
  std::string explanation;

  [[nodiscard]] bool usable() const noexcept;
};

/// Kinds of difference reported between two hardware subjects.
enum class CapabilityDifferenceKind {
  Equivalent = 0,
  OnlyInLeft,
  OnlyInRight,
  SupportStateDiffers,
  EffectiveSupportDiffers,
  ValueDiffers,
  ConditionsDiffer,
  PrecisionDiffers,
  StalenessDiffers,
  TruthClassDiffers,
  ContradictionDiffers,
  DriverDependentDifference,
  FirmwareDependentDifference,
  RuntimeDependentDifference,
  ArchitectureMismatch,
  ShapeDiffers,
};

const char* to_string(CapabilityDifferenceKind value) noexcept;

struct CapabilityDifference {
  CapabilityId capability;
  CapabilityDifferenceKind kind = CapabilityDifferenceKind::Equivalent;
  EffectiveSupport left_effective = EffectiveSupport::Unknown;
  EffectiveSupport right_effective = EffectiveSupport::Unknown;
  std::string detail;
};

/// Deterministic comparison of two hardware subjects.
///
/// The registry reports facts only. It never ranks one device above another:
/// there is no "better" field, and ordering is only provided for stable
/// rendering.
struct DeviceComparison {
  DeviceId left;
  DeviceId right;
  DeviceGeneration left_generation;
  DeviceGeneration right_generation;
  std::string left_software;
  std::string right_software;
  std::vector<CapabilityDifference> differences;
  std::vector<CapabilityId> equivalent;
  std::string explanation;
};

}  // namespace hcr
