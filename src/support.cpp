// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/support.hpp"

#include <cstring>

namespace hcr {
namespace {

bool equals_ci(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

}  // namespace

const char* to_string(SupportState state) noexcept {
  switch (state) {
    case SupportState::SupportedNative: return "SUPPORTED_NATIVE";
    case SupportState::SupportedConditional: return "SUPPORTED_CONDITIONAL";
    case SupportState::SupportedEmulated: return "SUPPORTED_EMULATED";
    case SupportState::SupportedSoftwareAssisted: return "SUPPORTED_SOFTWARE_ASSISTED";
    case SupportState::SupportedExperimental: return "SUPPORTED_EXPERIMENTAL";
    case SupportState::Disabled: return "DISABLED";
    case SupportState::Unsupported: return "UNSUPPORTED";
    case SupportState::Unknown: return "UNKNOWN";
    case SupportState::RevalidationRequired: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}

bool support_state_from_string(std::string_view text, SupportState& out) noexcept {
  struct Entry { const char* name; SupportState value; };
  static const Entry kEntries[] = {
      {"SUPPORTED_NATIVE", SupportState::SupportedNative},
      {"SUPPORTED_CONDITIONAL", SupportState::SupportedConditional},
      {"SUPPORTED_EMULATED", SupportState::SupportedEmulated},
      {"SUPPORTED_SOFTWARE_ASSISTED", SupportState::SupportedSoftwareAssisted},
      {"SUPPORTED_EXPERIMENTAL", SupportState::SupportedExperimental},
      {"DISABLED", SupportState::Disabled},
      {"UNSUPPORTED", SupportState::Unsupported},
      {"UNKNOWN", SupportState::Unknown},
      {"REVALIDATION_REQUIRED", SupportState::RevalidationRequired},
  };
  for (const Entry& entry : kEntries) {
    if (equals_ci(text, entry.name)) {
      out = entry.value;
      return true;
    }
  }
  return false;
}

bool asserts_capability_present(SupportState state) noexcept {
  switch (state) {
    case SupportState::SupportedNative:
    case SupportState::SupportedConditional:
    case SupportState::SupportedEmulated:
    case SupportState::SupportedSoftwareAssisted:
    case SupportState::SupportedExperimental:
    case SupportState::Disabled:
      return true;
    default:
      return false;
  }
}

bool asserts_capability_absent(SupportState state) noexcept {
  return state == SupportState::Unsupported;
}

int support_state_strength(SupportState state) noexcept {
  switch (state) {
    case SupportState::SupportedNative: return 7;
    case SupportState::SupportedConditional: return 6;
    case SupportState::SupportedEmulated: return 5;
    case SupportState::SupportedSoftwareAssisted: return 4;
    case SupportState::SupportedExperimental: return 3;
    case SupportState::Disabled: return 2;
    case SupportState::Unsupported: return 1;
    case SupportState::Unknown: return 0;
    case SupportState::RevalidationRequired: return -1;
  }
  return 0;
}

const char* to_string(EffectiveSupport state) noexcept {
  switch (state) {
    case EffectiveSupport::Supported: return "SUPPORTED";
    case EffectiveSupport::SupportedConditionalUnmet: return "SUPPORTED_CONDITIONAL_UNMET";
    case EffectiveSupport::SupportedConditionalUnknown: return "SUPPORTED_CONDITIONAL_UNKNOWN";
    case EffectiveSupport::SupportedWithCaveat: return "SUPPORTED_WITH_CAVEAT";
    case EffectiveSupport::BlockedByQuirk: return "BLOCKED_BY_QUIRK";
    case EffectiveSupport::Disabled: return "DISABLED";
    case EffectiveSupport::Unsupported: return "UNSUPPORTED";
    case EffectiveSupport::Unknown: return "UNKNOWN";
    case EffectiveSupport::RevalidationRequired: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}

const char* to_string(ContradictionState state) noexcept {
  switch (state) {
    case ContradictionState::Consistent: return "CONSISTENT";
    case ContradictionState::ConflictingEvidence: return "CONFLICTING_EVIDENCE";
    case ContradictionState::PreferredLiveEvidence: return "PREFERRED_LIVE_EVIDENCE";
    case ContradictionState::PreferredHigherAuthoritySource: return "PREFERRED_HIGHER_AUTHORITY_SOURCE";
    case ContradictionState::Ambiguous: return "AMBIGUOUS";
    case ContradictionState::RevalidationRequired: return "REVALIDATION_REQUIRED";
  }
  return "CONSISTENT";
}

const char* to_string(TruthClass value) noexcept {
  switch (value) {
    case TruthClass::Real: return "REAL";
    case TruthClass::Synthetic: return "SYNTHETIC";
    case TruthClass::Unsupported: return "UNSUPPORTED";
    case TruthClass::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* to_string(PrecisionClass value) noexcept {
  switch (value) {
    case PrecisionClass::Exact: return "EXACT";
    case PrecisionClass::Probed: return "PROBED";
    case PrecisionClass::DirectReported: return "DIRECT_REPORTED";
    case PrecisionClass::Derived: return "DERIVED";
    case PrecisionClass::Curated: return "CURATED";
    case PrecisionClass::Inferred: return "INFERRED";
    case PrecisionClass::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

bool precision_class_from_string(std::string_view text, PrecisionClass& out) noexcept {
  struct Entry { const char* name; PrecisionClass value; };
  static const Entry kEntries[] = {
      {"EXACT", PrecisionClass::Exact},
      {"PROBED", PrecisionClass::Probed},
      {"DIRECT_REPORTED", PrecisionClass::DirectReported},
      {"DERIVED", PrecisionClass::Derived},
      {"CURATED", PrecisionClass::Curated},
      {"INFERRED", PrecisionClass::Inferred},
      {"UNKNOWN", PrecisionClass::Unknown},
  };
  for (const Entry& entry : kEntries) {
    if (equals_ci(text, entry.name)) {
      out = entry.value;
      return true;
    }
  }
  return false;
}

int precision_class_rank(PrecisionClass value) noexcept {
  switch (value) {
    case PrecisionClass::Exact: return 6;
    case PrecisionClass::Probed: return 5;
    case PrecisionClass::DirectReported: return 4;
    case PrecisionClass::Derived: return 3;
    case PrecisionClass::Curated: return 2;
    case PrecisionClass::Inferred: return 1;
    case PrecisionClass::Unknown: return 0;
  }
  return 0;
}

bool precision_is_device_evidence(PrecisionClass value) noexcept {
  switch (value) {
    case PrecisionClass::Exact:
    case PrecisionClass::Probed:
    case PrecisionClass::DirectReported:
      return true;
    default:
      return false;
  }
}

}  // namespace hcr
