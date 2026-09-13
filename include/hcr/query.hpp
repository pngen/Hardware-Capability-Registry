// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/condition.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/quirk.hpp"
#include "hcr/resolution.hpp"
#include "hcr/support.hpp"

namespace hcr {

/// One device, one capability.
struct CapabilityQuery {
  DeviceId device;
  /// Optional generation pin. When set to a non-current generation the answer
  /// describes the historical generation and is marked not current.
  DeviceGeneration device_generation;
  CapabilityId capability;
  /// Environment used to evaluate conditional capability. Absent facts
  /// evaluate to Unknown; they never become Satisfied.
  EnvironmentContext environment;
  /// Include individual evidence records (including losing and stale ones).
  bool include_evidence = true;
};

/// One device, many capabilities.
struct DeviceCapabilityQuery {
  DeviceId device;
  DeviceGeneration device_generation;
  EnvironmentContext environment;
  std::string capability_prefix;
  std::optional<CapabilityDomain> domain;
  std::optional<SupportState> declared_filter;
  std::optional<EffectiveSupport> effective_filter;
  std::optional<TruthClass> truth_filter;
  bool only_conditional = false;
  bool only_unknown = false;
  bool only_unsupported = false;
  bool only_stale = false;
  bool only_current = true;
  std::size_t max_results = 0;  ///< zero means the registry limit
};

/// Which devices support a capability.
struct FleetCapabilityQuery {
  CapabilityId capability;
  EnvironmentContext environment;
  std::optional<EffectiveSupport> effective_filter;
  std::optional<TruthClass> truth_filter;
  std::optional<HardwareClass> hardware_class;
  bool only_current = true;
  std::size_t max_results = 0;
};

/// Which capabilities of a device became stale after a generation change.
struct StaleCapabilityQuery {
  DeviceId device;
  DeviceGeneration device_generation;
  EnvironmentContext environment;
  std::size_t max_results = 0;
};

/// Quirk listing/evaluation.
struct QuirkQuery {
  /// Restrict to quirks that impact this device.
  std::optional<DeviceId> device;
  EnvironmentContext environment;
  /// Restrict to quirks impacting this capability.
  std::optional<CapabilityId> capability;
  bool only_applicable = false;
  std::size_t max_results = 0;
};

/// Compatibility facts involving a hardware reference.
struct CompatibilityQuery {
  std::optional<HardwareReference> reference;
  std::optional<CompatibilitySubjectKind> kind;
  bool include_withdrawn = false;
  std::size_t max_results = 0;
};

/// Comparison request.
struct ComparisonRequest {
  DeviceId left;
  DeviceId right;
  DeviceGeneration left_generation;
  DeviceGeneration right_generation;
  EnvironmentContext environment;
};

/// Publisher listing filter.
struct PublisherQuery {
  std::optional<PublisherState> state;
  std::size_t max_results = 0;
};

}  // namespace hcr
