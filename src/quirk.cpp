// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/quirk.hpp"

#include <algorithm>

#include "hcr/canonical.hpp"

namespace hcr {
namespace {

template <typename T>
bool contains(const std::vector<T>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

int compare_revision_token(std::string_view a, std::string_view b) {
  const SoftwareVersion left = SoftwareVersion::parse(a);
  const SoftwareVersion right = SoftwareVersion::parse(b);
  if (left.valid() && right.valid()) return SoftwareVersion::compare(left, right);
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

std::string describe_ranges(const std::vector<VersionRange>& ranges) {
  std::string out;
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (i != 0) out += "|";
    out += ranges[i].describe();
  }
  return out;
}

}  // namespace

const char* to_string(QuirkCategory value) noexcept {
  switch (value) {
    case QuirkCategory::CapabilityMisreport: return "capability-misreport";
    case QuirkCategory::DriverDefect: return "driver-defect";
    case QuirkCategory::FirmwareDefect: return "firmware-defect";
    case QuirkCategory::RevisionLimitation: return "revision-limitation";
    case QuirkCategory::RequiredWorkaround: return "required-workaround";
    case QuirkCategory::BrokenFeatureCombination: return "broken-feature-combination";
    case QuirkCategory::PerformanceCaveat: return "performance-caveat";
    case QuirkCategory::ResetRequirement: return "reset-requirement";
    case QuirkCategory::UnsupportedCombination: return "unsupported-combination";
    case QuirkCategory::VersionIncompatibility: return "version-incompatibility";
    case QuirkCategory::Other: return "other";
  }
  return "other";
}

bool quirk_category_from_string(std::string_view text, QuirkCategory& out) noexcept {
  for (int i = 0; i <= static_cast<int>(QuirkCategory::Other); ++i) {
    const auto value = static_cast<QuirkCategory>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

const char* to_string(QuirkSeverity value) noexcept {
  switch (value) {
    case QuirkSeverity::Informational: return "informational";
    case QuirkSeverity::Caveat: return "caveat";
    case QuirkSeverity::Degraded: return "degraded";
    case QuirkSeverity::Unusable: return "unusable";
    case QuirkSeverity::Fatal: return "fatal";
  }
  return "informational";
}

bool quirk_severity_from_string(std::string_view text, QuirkSeverity& out) noexcept {
  for (int i = 0; i <= static_cast<int>(QuirkSeverity::Fatal); ++i) {
    const auto value = static_cast<QuirkSeverity>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

const char* to_string(MitigationKind value) noexcept {
  switch (value) {
    case MitigationKind::None: return "none";
    case MitigationKind::UpgradeDriver: return "upgrade-driver";
    case MitigationKind::UpgradeFirmware: return "upgrade-firmware";
    case MitigationKind::UpgradeRuntime: return "upgrade-runtime";
    case MitigationKind::ChangeDeviceMode: return "change-device-mode";
    case MitigationKind::ChangePlatformSetting: return "change-platform-setting";
    case MitigationKind::DisableFeature: return "disable-feature";
    case MitigationKind::UseAlternateCapability: return "use-alternate-capability";
    case MitigationKind::ResetDevice: return "reset-device";
    case MitigationKind::ContactVendor: return "contact-vendor";
  }
  return "none";
}

bool mitigation_kind_from_string(std::string_view text, MitigationKind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(MitigationKind::ContactVendor); ++i) {
    const auto value = static_cast<MitigationKind>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

const char* to_string(QuirkApplicabilityResult value) noexcept {
  switch (value) {
    case QuirkApplicabilityResult::Applies: return "applies";
    case QuirkApplicabilityResult::DoesNotApply: return "does-not-apply";
    case QuirkApplicabilityResult::Unknown: return "unknown";
  }
  return "unknown";
}

Status validate_quirk_applicability(const QuirkApplicability& applicability, const RegistryLimits& limits) {
  const auto check_ids = [](const auto& values, const char* what) -> Status {
    if (values.size() > 1024) {
      return Status::failure(ErrorCode::LimitExceeded, std::string("quirk ") + what + " selector too large");
    }
    for (const auto& value : values) {
      if (!value.valid()) {
        return Status::failure(ErrorCode::InvalidIdentity, std::string("quirk ") + what + " selector invalid");
      }
    }
    return Status::success();
  };

  Status status = check_ids(applicability.vendors, "vendor");
  if (!status.ok()) return status;
  status = check_ids(applicability.families, "family");
  if (!status.ok()) return status;
  status = check_ids(applicability.models, "model");
  if (!status.ok()) return status;
  status = check_ids(applicability.devices, "device");
  if (!status.ok()) return status;
  status = check_ids(applicability.revisions, "revision");
  if (!status.ok()) return status;
  status = check_ids(applicability.architectures, "architecture");
  if (!status.ok()) return status;

  if (applicability.revision_ranges.size() > 64 || applicability.driver_ranges.size() > 64 ||
      applicability.firmware_ranges.size() > 64 || applicability.runtime_ranges.size() > 64) {
    return Status::failure(ErrorCode::LimitExceeded, "quirk range selector too large");
  }
  for (const RevisionRange& range : applicability.revision_ranges) {
    if (range.minimum.empty() || range.maximum.empty()) {
      return Status::failure(ErrorCode::InvalidRange, "revision range requires both bounds");
    }
    if (range.minimum.size() > limits.max_metadata_bytes || range.maximum.size() > limits.max_metadata_bytes) {
      return Status::failure(ErrorCode::LimitExceeded, "revision bound exceeds length limit");
    }
    if (compare_revision_token(range.minimum, range.maximum) > 0) {
      return Status::failure(ErrorCode::InvalidRange, "revision range minimum exceeds maximum");
    }
  }
  const auto check_ranges = [](const std::vector<VersionRange>& ranges) -> Status {
    for (const VersionRange& range : ranges) {
      if (!range.valid()) {
        return Status::failure(ErrorCode::InvalidRange, "quirk version range is inverted");
      }
      if (!range.minimum.valid() && !range.maximum.valid()) {
        return Status::failure(ErrorCode::InvalidRange, "quirk version range has no bound");
      }
    }
    return Status::success();
  };
  status = check_ranges(applicability.driver_ranges);
  if (!status.ok()) return status;
  status = check_ranges(applicability.firmware_ranges);
  if (!status.ok()) return status;
  status = check_ranges(applicability.runtime_ranges);
  if (!status.ok()) return status;

  if (applicability.platform_conditions != nullptr) {
    return validate_condition(*applicability.platform_conditions, limits);
  }
  return Status::success();
}

Status validate_quirk_record(const QuirkRecord& record, const RegistryLimits& limits) {
  if (!record.id.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "quirk requires a canonical identity");
  }
  if (!record.quirk_generation.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "quirk requires a generation");
  }
  if (record.title.empty() || record.title.size() > limits.max_name_length) {
    return Status::failure(ErrorCode::InvalidArgument, "quirk requires a bounded title");
  }
  if (record.description.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "quirk description exceeds length limit");
  }
  if (record.impacted_capabilities.empty()) {
    return Status::failure(ErrorCode::InvalidCapability, "quirk must impact at least one capability");
  }
  if (record.impacted_capabilities.size() > 64) {
    return Status::failure(ErrorCode::LimitExceeded, "quirk impacts too many capabilities");
  }
  for (const CapabilityId& capability : record.impacted_capabilities) {
    if (!capability.valid()) {
      return Status::failure(ErrorCode::InvalidCapability, "quirk impacts an invalid capability identity");
    }
  }
  if (!record.source.publisher.id.valid()) {
    return Status::failure(ErrorCode::InvalidProvenance, "quirk requires a publisher identity");
  }
  if (record.source.adapter.empty() || record.source.adapter.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::InvalidProvenance, "quirk requires a bounded source adapter");
  }
  if (record.source.source_class == SourceClass::Unspecified) {
    return Status::failure(ErrorCode::InvalidProvenance, "quirk requires a source class");
  }
  if (!provenance_allowed_for(record.source.provenance, record.source.source_class)) {
    return Status::failure(ErrorCode::InvalidProvenance,
                           "quirk provenance is stronger than its source class can support");
  }
  return validate_quirk_applicability(record.applicability, limits);
}

bool quirk_precedes(const QuirkRecord& a, const QuirkRecord& b) noexcept {
  if (a.severity != b.severity) return static_cast<int>(a.severity) > static_cast<int>(b.severity);
  if (a.id != b.id) return a.id < b.id;
  return a.quirk_generation < b.quirk_generation;
}

QuirkApplication evaluate_quirk(const QuirkRecord& quirk,
                                const DeviceIdentity& device,
                                const DeviceSoftwareState& software,
                                const EnvironmentContext& environment) {
  QuirkApplication application;
  application.id = quirk.id;
  application.quirk_generation = quirk.quirk_generation;
  application.severity = quirk.severity;
  application.mitigation = quirk.mitigation;
  application.title = quirk.title;

  std::string reason;
  bool unknown = false;
  const auto fail = [&](std::string text) {
    application.result = QuirkApplicabilityResult::DoesNotApply;
    application.reason = std::move(text);
    return application;
  };
  const auto defer = [&](std::string text) {
    unknown = true;
    reason = std::move(text);
  };

  if (!quirk.applicability.devices.empty()) {
    if (!contains(quirk.applicability.devices, device.id)) {
      return fail("device not selected");
    }
    reason = "device selected";
  }
  if (!quirk.applicability.vendors.empty()) {
    if (!device.hardware.vendor.valid() || !contains(quirk.applicability.vendors, device.hardware.vendor)) {
      return fail("vendor not selected");
    }
  }
  if (!quirk.applicability.families.empty()) {
    if (!device.hardware.family.valid() || !contains(quirk.applicability.families, device.hardware.family)) {
      return fail("family not selected");
    }
  }
  if (!quirk.applicability.models.empty()) {
    if (!device.hardware.model.valid() || !contains(quirk.applicability.models, device.hardware.model)) {
      return fail("model not selected");
    }
  }
  if (!quirk.applicability.architectures.empty()) {
    if (!device.hardware.architecture.valid()) {
      defer("device architecture unknown");
    } else if (!contains(quirk.applicability.architectures, device.hardware.architecture)) {
      return fail("architecture not selected");
    }
  }
  if (!quirk.applicability.revisions.empty()) {
    if (!device.hardware.revision.valid()) {
      defer("device revision unknown");
    } else if (!contains(quirk.applicability.revisions, device.hardware.revision)) {
      return fail("revision not selected");
    }
  }
  if (!quirk.applicability.revision_ranges.empty()) {
    if (!device.hardware.revision.valid()) {
      defer("device revision unknown");
    } else {
      bool matched = false;
      for (const RevisionRange& range : quirk.applicability.revision_ranges) {
        if (compare_revision_token(device.hardware.revision.str(), range.minimum) >= 0 &&
            compare_revision_token(device.hardware.revision.str(), range.maximum) <= 0) {
          matched = true;
          break;
        }
      }
      if (!matched) return fail("revision outside range");
    }
  }
  const auto evaluate_ranges = [&](const std::vector<VersionRange>& ranges, const std::string& observed,
                                   const char* axis) -> bool {
    if (ranges.empty()) return true;
    if (observed.empty()) {
      defer(std::string("device ") + axis + " version unknown");
      return true;
    }
    const SoftwareVersion version = SoftwareVersion::parse(observed);
    if (!version.valid()) {
      defer(std::string("device ") + axis + " version unparseable");
      return true;
    }
    for (const VersionRange& range : ranges) {
      if (range.contains(version)) return true;
    }
    application.result = QuirkApplicabilityResult::DoesNotApply;
    application.reason = std::string(axis) + " version " + version.raw() + " outside " + describe_ranges(ranges);
    return false;
  };

  if (!evaluate_ranges(quirk.applicability.driver_ranges, software.driver.version, "driver")) {
    return application;
  }
  if (!evaluate_ranges(quirk.applicability.firmware_ranges, software.firmware.version, "firmware")) {
    return application;
  }
  if (!evaluate_ranges(quirk.applicability.runtime_ranges, software.runtime.version, "runtime")) {
    return application;
  }

  if (quirk.applicability.platform_conditions != nullptr) {
    const ConditionEvaluation evaluation = evaluate_condition(quirk.applicability.platform_conditions.get(), environment);
    if (evaluation.outcome == ConditionOutcome::Unsatisfied) {
      return fail("platform condition unsatisfied: " +
                  describe_condition(quirk.applicability.platform_conditions.get()));
    }
    if (evaluation.outcome == ConditionOutcome::Unknown) {
      defer("platform condition unknown: " +
            describe_condition(quirk.applicability.platform_conditions.get()));
    }
  }

  application.result = unknown ? QuirkApplicabilityResult::Unknown : QuirkApplicabilityResult::Applies;
  application.reason = unknown ? reason : "all applicability selectors matched";
  return application;
}

}  // namespace hcr
