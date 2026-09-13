// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace hcr {

/// Explicit support states. Support is never reduced to a boolean.
enum class SupportState {
  SupportedNative = 0,
  SupportedConditional,
  SupportedEmulated,
  SupportedSoftwareAssisted,
  SupportedExperimental,
  Disabled,
  Unsupported,
  Unknown,
  RevalidationRequired,
};

const char* to_string(SupportState state) noexcept;
bool support_state_from_string(std::string_view text, SupportState& out) noexcept;

/// True only for states that assert the capability exists in some form.
bool asserts_capability_present(SupportState state) noexcept;

/// True when the state is evidence of absence rather than absence of evidence.
bool asserts_capability_absent(SupportState state) noexcept;

/// Ordering used when deterministic tie-breaking must prefer a "stronger"
/// assertion. Unknown and RevalidationRequired are weakest; Unsupported is
/// deliberately not treated as "stronger" than Unsupported.
int support_state_strength(SupportState state) noexcept;

/// Effective support after conditions, quirks and staleness are applied to the
/// declared support state. Declared state is preserved separately so that
/// "supported by hardware, unusable under this driver" stays visible.
enum class EffectiveSupport {
  Supported = 0,
  SupportedConditionalUnmet,
  SupportedConditionalUnknown,
  SupportedWithCaveat,
  BlockedByQuirk,
  Disabled,
  Unsupported,
  Unknown,
  RevalidationRequired,
};

const char* to_string(EffectiveSupport state) noexcept;

/// Contradiction states produced when evidence disagrees.
enum class ContradictionState {
  Consistent = 0,
  ConflictingEvidence,
  PreferredLiveEvidence,
  PreferredHigherAuthoritySource,
  Ambiguous,
  RevalidationRequired,
};

const char* to_string(ContradictionState state) noexcept;

/// User-facing truth classification. Every hardware-facing statement is
/// exactly one of these; SYNTHETIC never means UNKNOWN, and UNSUPPORTED
/// requires positive evidence of non-support.
enum class TruthClass {
  Real = 0,
  Synthetic,
  Unsupported,
  Unknown,
};

const char* to_string(TruthClass value) noexcept;

/// Strength of a capability statement; inferred family-wide knowledge is never
/// reported at the precision of exact device evidence.
enum class PrecisionClass {
  Exact = 0,
  Probed,
  DirectReported,
  Derived,
  Curated,
  Inferred,
  Unknown,
};

const char* to_string(PrecisionClass value) noexcept;
bool precision_class_from_string(std::string_view text, PrecisionClass& out) noexcept;
int precision_class_rank(PrecisionClass value) noexcept;

/// True when the precision asserts a device-specific observation.
bool precision_is_device_evidence(PrecisionClass value) noexcept;

}  // namespace hcr
