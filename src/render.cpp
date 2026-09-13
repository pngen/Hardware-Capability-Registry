// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/render.hpp"

#include <algorithm>

#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"

namespace hcr {
namespace {

std::string number(std::size_t value) { return std::to_string(value); }

void hash_line(std::uint64_t& hash, std::string_view text) {
  fnv1a64_mix(hash, text);
}

}  // namespace

const char* to_string(CapabilityDifferenceKind value) noexcept {
  switch (value) {
    case CapabilityDifferenceKind::Equivalent: return "equivalent";
    case CapabilityDifferenceKind::OnlyInLeft: return "only-in-left";
    case CapabilityDifferenceKind::OnlyInRight: return "only-in-right";
    case CapabilityDifferenceKind::SupportStateDiffers: return "support-state-differs";
    case CapabilityDifferenceKind::EffectiveSupportDiffers: return "effective-support-differs";
    case CapabilityDifferenceKind::ValueDiffers: return "value-differs";
    case CapabilityDifferenceKind::ConditionsDiffer: return "conditions-differ";
    case CapabilityDifferenceKind::PrecisionDiffers: return "precision-differs";
    case CapabilityDifferenceKind::StalenessDiffers: return "staleness-differs";
    case CapabilityDifferenceKind::TruthClassDiffers: return "truth-class-differs";
    case CapabilityDifferenceKind::ContradictionDiffers: return "contradiction-differs";
    case CapabilityDifferenceKind::DriverDependentDifference: return "driver-dependent-difference";
    case CapabilityDifferenceKind::FirmwareDependentDifference: return "firmware-dependent-difference";
    case CapabilityDifferenceKind::RuntimeDependentDifference: return "runtime-dependent-difference";
    case CapabilityDifferenceKind::ArchitectureMismatch: return "architecture-mismatch";
    case CapabilityDifferenceKind::ShapeDiffers: return "shape-differs";
  }
  return "equivalent";
}

bool ResolvedCapability::usable() const noexcept {
  return effective_support == EffectiveSupport::Supported ||
         effective_support == EffectiveSupport::SupportedWithCaveat;
}

std::string render_truth_class(TruthClass value) { return to_string(value); }

std::string render_capability_brief(const ResolvedCapability& value) {
  std::string out;
  out += value.capability.str();
  out += " = ";
  out += to_string(value.effective_support);
  out += " [declared ";
  out += to_string(value.declared_support);
  out += ", truth ";
  out += to_string(value.truth);
  out += ", precision ";
  out += to_string(value.precision);
  out += ", generation ";
  out += value.capability_generation.valid() ? value.capability_generation.str() : std::string("-");
  if (is_stale(value.staleness)) {
    out += ", stale ";
    out += describe_staleness(value.staleness);
  }
  if (value.contradiction != ContradictionState::Consistent) {
    out += ", contradiction ";
    out += to_string(value.contradiction);
  }
  if (!value.value.empty()) {
    out += ", value ";
    out += value.value.describe();
  }
  if (value.conditions != nullptr) {
    out += ", conditions ";
    out += describe_condition(value.conditions.get());
    out += " (";
    out += to_string(value.condition_evaluation.outcome);
    out += ")";
  }
  for (const QuirkApplication& application : value.quirks) {
    if (application.result != QuirkApplicabilityResult::Applies) continue;
    out += ", quirk ";
    out += application.id.str();
    out += " (";
    out += to_string(application.severity);
    out += ")";
  }
  out += "]";
  return out;
}

std::string render_capability(const ResolvedCapability& value) {
  std::string out = value.explanation;
  return out;
}

std::string render_subject_brief(const SubjectSnapshot& subject) {
  std::string out;
  out += subject.identity.id.str();
  out += " | class=";
  out += to_string(subject.identity.hardware.hardware_class);
  out += " | vendor=";
  out += subject.identity.hardware.vendor.valid() ? subject.identity.hardware.vendor.str() : std::string("-");
  out += " | model=";
  out += subject.identity.hardware.model.valid() ? subject.identity.hardware.model.str() : std::string("-");
  out += " | generation=";
  out += subject.generation.str();
  out += " | boot=";
  out += subject.boot.str();
  out += " | driver=";
  out += subject.software.driver.id.valid() ? subject.software.driver.id.str() : std::string("-");
  out += "@";
  out += subject.software.driver.version.empty() ? std::string("-") : subject.software.driver.version;
  out += " | firmware=";
  out += subject.software.firmware.version.empty() ? std::string("-") : subject.software.firmware.version;
  out += " | classification=";
  out += to_string(subject.classification);
  return out;
}

std::string render_subject(const SubjectSnapshot& subject) {
  std::string out = render_subject_brief(subject);
  out += "\n  display: ";
  out += subject.identity.display_name.empty() ? std::string("(none)") : subject.identity.display_name;
  out += "\n  family=";
  out += subject.identity.hardware.family.valid() ? subject.identity.hardware.family.str() : std::string("-");
  out += " revision=";
  out += subject.identity.hardware.revision.valid() ? subject.identity.hardware.revision.str() : std::string("-");
  out += " architecture=";
  out += subject.identity.hardware.architecture.valid() ? subject.identity.hardware.architecture.str()
                                                        : std::string("-");
  out += "\n  incarnation=";
  out += subject.identity.incarnation.str();
  out += " platform=";
  out += subject.identity.platform.valid() ? subject.identity.platform.str() : std::string("-");
  out += " platform-generation=";
  out += subject.platform_generation.str();
  out += "\n  serial=";
  out += subject.identity.serial_number.has_value() ? *subject.identity.serial_number : std::string("(absent)");
  out += " instance-locator=";
  out += subject.identity.instance_locator.has_value() ? *subject.identity.instance_locator
                                                      : std::string("(absent)");
  out += "\n  runtime=";
  out += subject.software.runtime.id.valid() ? subject.software.runtime.id.str() : std::string("-");
  out += "@";
  out += subject.software.runtime.version.empty() ? std::string("-") : subject.software.runtime.version;
  out += "\n  capabilities=";
  out += number(subject.capability_count);
  out += " current=";
  out += number(subject.current_count);
  out += " stale=";
  out += number(subject.stale_count);
  out += " unknown=";
  out += number(subject.unknown_count);
  out += " unsupported=";
  out += number(subject.unsupported_count);
  out += " applicable-quirks=";
  out += number(subject.applicable_quirk_count);
  for (const NativeIdentifier& identifier : subject.identity.hardware.native_identifiers) {
    out += "\n  native ";
    out += identifier.key;
    out += "=";
    out += identifier.value;
  }
  return out;
}

std::string render_quirk(const QuirkRecord& quirk) {
  std::string out;
  out += quirk.id.str();
  out += " generation=";
  out += quirk.quirk_generation.str();
  out += " severity=";
  out += to_string(quirk.severity);
  out += " category=";
  out += to_string(quirk.category);
  out += " mitigation=";
  out += to_string(quirk.mitigation);
  out += quirk.withdrawn ? " [withdrawn]" : "";
  out += "\n  title: ";
  out += quirk.title;
  if (!quirk.description.empty()) {
    out += "\n  description: ";
    out += quirk.description;
  }
  out += "\n  impacts:";
  for (const CapabilityId& capability : quirk.impacted_capabilities) {
    out += " ";
    out += capability.str();
  }
  out += "\n  applies-to: devices=";
  out += number(quirk.applicability.devices.size());
  out += " families=";
  out += number(quirk.applicability.families.size());
  out += " models=";
  out += number(quirk.applicability.models.size());
  out += " revisions=";
  out += number(quirk.applicability.revisions.size() + quirk.applicability.revision_ranges.size());
  out += " driver-ranges=";
  out += number(quirk.applicability.driver_ranges.size());
  out += " firmware-ranges=";
  out += number(quirk.applicability.firmware_ranges.size());
  out += " runtime-ranges=";
  out += number(quirk.applicability.runtime_ranges.size());
  if (quirk.applicability.platform_conditions != nullptr) {
    out += " platform-condition=";
    out += describe_condition(quirk.applicability.platform_conditions.get());
  }
  out += "\n  provenance: ";
  out += to_string(quirk.source.provenance);
  out += " source=";
  out += to_string(quirk.source.source_class);
  out += " adapter=";
  out += quirk.source.adapter;
  if (!quirk.source.native_reference.empty()) {
    out += " reference=";
    out += quirk.source.native_reference;
  }
  return out;
}

std::string render_quirk_application(const QuirkApplication& application) {
  std::string out;
  out += application.id.str();
  out += " [";
  out += to_string(application.result);
  out += ", severity ";
  out += to_string(application.severity);
  out += ", mitigation ";
  out += to_string(application.mitigation);
  out += "] ";
  out += application.title;
  out += " :: ";
  out += application.reason;
  return out;
}

std::string render_compatibility(const CompatibilityFact& fact) {
  std::string out;
  out += fact.id.str();
  out += " generation=";
  out += fact.compatibility_generation.str();
  out += " kind=";
  out += to_string(fact.kind);
  out += " outcome=";
  out += to_string(fact.outcome);
  out += fact.withdrawn ? " [withdrawn]" : "";
  out += "\n  ";
  out += to_string(fact.lhs.kind);
  out += ":";
  out += fact.lhs.token;
  if (fact.lhs.version.valid()) {
    out += "@";
    out += fact.lhs.version.raw();
  }
  out += " <-> ";
  out += to_string(fact.rhs.kind);
  out += ":";
  out += fact.rhs.token;
  if (fact.rhs.version.valid()) {
    out += "@";
    out += fact.rhs.version.raw();
  }
  if (!fact.reason.empty()) {
    out += "\n  reason: ";
    out += fact.reason;
  }
  if (fact.conditions != nullptr) {
    out += "\n  conditions: ";
    out += describe_condition(fact.conditions.get());
  }
  out += "\n  provenance: ";
  out += to_string(fact.source.provenance);
  out += " source=";
  out += to_string(fact.source.source_class);
  out += " adapter=";
  out += fact.source.adapter;
  return out;
}

std::string render_publisher(const PublisherRecord& publisher) {
  std::string out;
  out += publisher.identity.id.str();
  out += " boot=";
  out += publisher.identity.boot.valid() ? publisher.identity.boot.str() : std::string("-");
  out += " epoch=";
  out += publisher.identity.epoch.valid() ? publisher.identity.epoch.str() : std::string("-");
  out += " state=";
  out += to_string(publisher.state);
  out += " adapter=";
  out += publisher.adapter;
  out += " last-sequence=";
  out += std::to_string(publisher.last_sequence);
  out += " evidence-generation=";
  out += publisher.max_evidence_generation.valid() ? publisher.max_evidence_generation.str() : std::string("-");
  out += " accepted=";
  out += number(publisher.accepted);
  out += " rejected=";
  out += number(publisher.rejected);
  out += " boots=";
  out += number(publisher.boot_history);
  if (!publisher.reason.empty()) {
    out += " reason=\"";
    out += publisher.reason;
    out += "\"";
  }
  return out;
}

std::string render_snapshot_summary(const RegistrySnapshot& snapshot) {
  std::string out;
  out += "snapshot generation=";
  out += snapshot.generation.valid() ? snapshot.generation.str() : std::string("-");
  out += " epoch=";
  out += snapshot.epoch.valid() ? snapshot.epoch.str() : std::string("-");
  out += " revision=";
  out += std::to_string(snapshot.state_revision);
  out += " created=";
  out += snapshot.created_at_utc;
  out += " digest=";
  out += hex64(snapshot.canonical_digest);
  out += "\n  subjects=";
  out += number(snapshot.subjects.size());
  out += " capabilities=";
  out += number(snapshot.capabilities.size());
  out += " quirks=";
  out += number(snapshot.quirks.size());
  out += " compatibility=";
  out += number(snapshot.compatibility.size());
  out += " publishers=";
  out += number(snapshot.publishers.size());
  out += "\n  statements: REAL=";
  out += number(snapshot.real_statements);
  out += " SYNTHETIC=";
  out += number(snapshot.synthetic_statements);
  out += " UNSUPPORTED=";
  out += number(snapshot.unsupported_statements);
  out += " UNKNOWN=";
  out += number(snapshot.unknown_statements);
  out += " stale-evidence=";
  out += number(snapshot.stale_evidence_records);
  return out;
}

std::string render_comparison(const DeviceComparison& comparison) {
  std::string out;
  out += "compare ";
  out += comparison.left.str();
  out += " (generation ";
  out += comparison.left_generation.str();
  out += ", driver ";
  out += comparison.left_software;
  out += ") vs ";
  out += comparison.right.str();
  out += " (generation ";
  out += comparison.right_generation.str();
  out += ", driver ";
  out += comparison.right_software;
  out += ")";
  for (const CapabilityDifference& difference : comparison.differences) {
    out += "\n  ";
    out += difference.capability.str();
    out += ": ";
    out += to_string(difference.kind);
    if (!difference.detail.empty()) {
      out += " (";
      out += difference.detail;
      out += ")";
    }
  }
  out += "\n  equivalent=";
  out += number(comparison.equivalent.size());
  out += "\n  ";
  out += comparison.explanation;
  return out;
}

std::string render_resource_usage(const ResourceUsage& usage) {
  std::string out;
  out += "devices=";
  out += number(usage.devices);
  out += " platforms=";
  out += number(usage.platforms);
  out += " capability-keys=";
  out += number(usage.capability_keys);
  out += " evidence-records=";
  out += number(usage.evidence_records);
  out += " quirks=";
  out += number(usage.quirks);
  out += " compatibility=";
  out += number(usage.compatibility_facts);
  out += " publishers=";
  out += number(usage.publishers);
  out += " schemas=";
  out += number(usage.schemas);
  out += " accepted=";
  out += std::to_string(usage.accepted_publications);
  out += " idempotent=";
  out += std::to_string(usage.idempotent_publications);
  out += " rejected=";
  out += std::to_string(usage.rejected_publications);
  out += " snapshots=";
  out += std::to_string(usage.snapshots_taken);
  return out;
}

std::string render_import_report(const ImportReport& report) {
  std::string out;
  out += report.applied ? "import applied" : "import not applied";
  out += " subjects=";
  out += number(report.subjects);
  out += " schemas=";
  out += number(report.schemas);
  out += " capability-records=";
  out += number(report.capability_records);
  out += " revalidated=";
  out += number(report.revalidated_records);
  out += " quirks=";
  out += number(report.quirks);
  out += " compatibility=";
  out += number(report.compatibility_facts);
  out += " publishers=";
  out += number(report.publishers);
  out += " fenced-boots=";
  out += number(report.fenced_boots);
  if (!report.detail.empty()) {
    out += " :: ";
    out += report.detail;
  }
  return out;
}

std::uint64_t canonical_snapshot_digest(const RegistrySnapshot& snapshot) {
  std::uint64_t hash = kFnvOffsetBasis;
  hash_line(hash, "hcr.snapshot.v1");
  // The snapshot label and wall-clock timestamp are deliberately excluded: the
  // digest is a content digest, so two snapshots of unchanged state have the
  // same digest even though their generations differ.
  fnv1a64_mix(hash, snapshot.epoch.value());
  fnv1a64_mix(hash, snapshot.state_revision);
  for (const SubjectSnapshot& subject : snapshot.subjects) {
    hash_line(hash, subject.identity.id.str());
    hash_line(hash, subject.identity.incarnation.str());
    hash_line(hash, subject.identity.hardware.canonical_key());
    fnv1a64_mix(hash, subject.generation.value());
    fnv1a64_mix(hash, subject.boot.value());
    fnv1a64_mix(hash, subject.platform_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(subject.classification));
  }
  for (const ResolvedCapability& capability : snapshot.capabilities) {
    hash_line(hash, capability.device.str());
    hash_line(hash, capability.capability.str());
    hash_line(hash, capability.schema.str());
    fnv1a64_mix(hash, capability.capability_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(capability.declared_support));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(capability.effective_support));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(capability.truth));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(capability.contradiction));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(capability.staleness));
    hash_line(hash, capability.value.describe());
    hash_line(hash, describe_condition(capability.conditions.get()));
    for (const EvidenceView& view : capability.evidence) {
      hash_line(hash, view.source.publisher.id.str());
      fnv1a64_mix(hash, view.source.publisher.boot.value());
      fnv1a64_mix(hash, view.source.evidence_generation.value());
      fnv1a64_mix(hash, static_cast<std::uint64_t>(view.source.source_class));
      fnv1a64_mix(hash, view.winner ? 1u : 0u);
    }
  }
  for (const QuirkRecord& quirk : snapshot.quirks) {
    hash_line(hash, quirk.id.str());
    fnv1a64_mix(hash, quirk.quirk_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(quirk.severity));
  }
  for (const CompatibilityFact& fact : snapshot.compatibility) {
    hash_line(hash, fact.id.str());
    fnv1a64_mix(hash, fact.compatibility_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(fact.outcome));
  }
  for (const PublisherRecord& publisher : snapshot.publishers) {
    hash_line(hash, publisher.identity.id.str());
    fnv1a64_mix(hash, publisher.identity.boot.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(publisher.state));
  }
  return hash;
}

std::uint64_t canonical_durable_digest(const DurableState& state) {
  std::uint64_t hash = kFnvOffsetBasis;
  hash_line(hash, "hcr.durable.v1");
  fnv1a64_mix(hash, state.header.format_version);
  fnv1a64_mix(hash, state.header.epoch.value());
  fnv1a64_mix(hash, state.header.snapshot_generation.value());
  fnv1a64_mix(hash, state.header.state_revision);
  hash_line(hash, state.header.created_at_utc);
  hash_line(hash, state.header.producer);
  for (const PlatformIdentity& platform : state.platforms) {
    hash_line(hash, platform.id.str());
    hash_line(hash, platform.display_name);
    hash_line(hash, platform.architecture.str());
  }
  for (const SubjectSnapshot& subject : state.subjects) {
    hash_line(hash, subject.identity.id.str());
    hash_line(hash, subject.identity.incarnation.str());
    hash_line(hash, subject.identity.hardware.canonical_key());
    hash_line(hash, subject.identity.platform.str());
    hash_line(hash, subject.identity.display_name);
    fnv1a64_mix(hash, subject.generation.value());
    fnv1a64_mix(hash, subject.boot.value());
    fnv1a64_mix(hash, subject.platform_generation.value());
    hash_line(hash, subject.software.driver.id.str());
    hash_line(hash, subject.software.driver.version);
    hash_line(hash, subject.software.firmware.id.str());
    hash_line(hash, subject.software.firmware.version);
    hash_line(hash, subject.software.runtime.id.str());
    hash_line(hash, subject.software.runtime.version);
  }
  for (const CapabilitySchema& schema : state.schemas) {
    hash_line(hash, schema.id.str());
    hash_line(hash, schema.capability.str());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(schema.value_kind));
  }
  for (const CapabilityRecord& record : state.capability_history) {
    hash_line(hash, record.device.str());
    hash_line(hash, record.capability.str());
    hash_line(hash, record.schema.str());
    fnv1a64_mix(hash, record.device_generation.value());
    fnv1a64_mix(hash, record.device_boot.value());
    fnv1a64_mix(hash, record.capability_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(record.support));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(record.precision));
    hash_line(hash, record.value.describe());
    hash_line(hash, describe_condition(record.conditions.get()));
    hash_line(hash, record.source.publisher.id.str());
    fnv1a64_mix(hash, record.source.publisher.boot.value());
    fnv1a64_mix(hash, record.source.publisher.epoch.value());
    fnv1a64_mix(hash, record.source.evidence_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(record.source.source_class));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(record.source.provenance));
    hash_line(hash, record.source.adapter);
    hash_line(hash, record.source.native_reference);
    fnv1a64_mix(hash, record.source.observation_sequence);
    hash_line(hash, record.driver_id.str());
    hash_line(hash, record.firmware_id.str());
    hash_line(hash, record.runtime_id.str());
    fnv1a64_mix(hash, record.driver_generation.value());
    fnv1a64_mix(hash, record.firmware_generation.value());
    fnv1a64_mix(hash, record.runtime_generation.value());
    fnv1a64_mix(hash, record.withdrawn ? 1u : 0u);
  }
  for (const QuirkRecord& quirk : state.quirks) {
    hash_line(hash, quirk.id.str());
    fnv1a64_mix(hash, quirk.quirk_generation.value());
    hash_line(hash, quirk.title);
    fnv1a64_mix(hash, static_cast<std::uint64_t>(quirk.severity));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(quirk.category));
    for (const CapabilityId& capability : quirk.impacted_capabilities) {
      hash_line(hash, capability.str());
    }
  }
  for (const CompatibilityFact& fact : state.compatibility) {
    hash_line(hash, fact.id.str());
    fnv1a64_mix(hash, fact.compatibility_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(fact.kind));
    fnv1a64_mix(hash, static_cast<std::uint64_t>(fact.outcome));
    hash_line(hash, fact.lhs.token);
    hash_line(hash, fact.rhs.token);
    hash_line(hash, fact.reason);
  }
  for (const PublisherWatermark& watermark : state.publisher_watermarks) {
    hash_line(hash, watermark.publisher.str());
    fnv1a64_mix(hash, watermark.boot.value());
    fnv1a64_mix(hash, watermark.max_sequence);
    fnv1a64_mix(hash, watermark.max_evidence_generation.value());
    fnv1a64_mix(hash, static_cast<std::uint64_t>(watermark.state));
  }
  for (const PublisherWatermark& fenced : state.fenced_boots) {
    hash_line(hash, fenced.publisher.str());
    fnv1a64_mix(hash, fenced.boot.value());
  }
  return hash;
}

StalenessFlag detect_snapshot_staleness(const RegistrySnapshot& snapshot, const RegistrySnapshot& current) {
  StalenessFlag flags = StalenessFlag::None;
  if (snapshot.epoch != current.epoch) flags |= StalenessFlag::EpochSuperseded;
  if (snapshot.state_revision != current.state_revision) {
    flags |= StalenessFlag::SupersededByNewerEvidence;
  }
  if (snapshot.generation != current.generation) flags |= StalenessFlag::SupersededByNewerEvidence;
  return flags;
}

bool snapshot_is_current(const RegistrySnapshot& snapshot, const RegistrySnapshot& current) {
  return detect_snapshot_staleness(snapshot, current) == StalenessFlag::None;
}

}  // namespace hcr
