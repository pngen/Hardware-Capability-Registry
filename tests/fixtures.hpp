// Hardware Capability Registry - shared test fixtures.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

#include "harness.hpp"
#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"
#include "hcr/discovery.hpp"
#include "hcr/registry.hpp"
#include "hcr/render.hpp"

namespace hcrtest {

/// Builds fully attributed publications against a real registry through the
/// production ingestion path. Tests never reach into registry internals.
class Fixture {
 public:
  explicit Fixture(const char* publisher_id = "publisher.test") {
    hcr::PublisherIdentity identity;
    identity.id = hcr::PublisherId::parse(publisher_id);
    const hcr::ApplyResult registered = registry.register_publisher(identity, "test.fixture");
    HCR_REQUIRE(registered.accepted());
    publisher = registered.assigned_publisher;
  }

  hcr::PublisherIdentity publisher;
  hcr::CapabilityRegistry registry;

  hcr::DeviceRegistration device_registration(const std::string& token,
                                              const char* driver_version = "1.0",
                                              const char* firmware_version = "1.0",
                                              hcr::HardwareClass hardware_class = hcr::HardwareClass::Gpu,
                                              const char* family = "family.test") const {
    hcr::DeviceRegistration registration;
    registration.identity.id = hcr::canonical_device_id("dev.test", token);
    registration.identity.incarnation =
        hcr::DeviceIncarnationId::parse("inc.test." + hcr::ascii_lower(token) + ".1");
    registration.identity.hardware.vendor = hcr::VendorId::parse("vendor.test");
    registration.identity.hardware.product = hcr::ProductId::parse("product." + hcr::ascii_lower(token));
    registration.identity.hardware.family = hcr::HardwareFamilyId::parse(family);
    registration.identity.hardware.model = hcr::HardwareModelId::parse("model." + hcr::ascii_lower(token));
    registration.identity.hardware.revision = hcr::HardwareRevisionId::parse("rev.test.a0");
    registration.identity.hardware.architecture = hcr::ArchitectureId::parse("arch.test.sm_10");
    registration.identity.hardware.hardware_class = hardware_class;
    registration.identity.hardware.native_identifiers = {{"test.token", token}};
    registration.identity.platform = hcr::PlatformId::parse("platform.test");
    registration.identity.display_name = token;
    registration.identity.instance_locator = token;
    registration.software.driver.id = hcr::DriverId::parse("driver.test");
    registration.software.driver.version = driver_version;
    registration.software.firmware.id = hcr::FirmwareId::parse("fw.test");
    registration.software.firmware.version = firmware_version;
    registration.software.runtime.id = hcr::RuntimeId::parse("runtime.test");
    registration.software.runtime.version = "1.0";
    registration.platform.id = hcr::PlatformId::parse("platform.test");
    registration.platform.display_name = "test platform";
    registration.platform.vendor = "vendor.test";
    registration.platform.model = "model.test";
    registration.platform.architecture = hcr::ArchitectureId::parse("arch.test.sm_10");
    registration.observed_at_utc = "2026-01-01T00:00:00Z";
    return registration;
  }

  hcr::ApplyResult add_device(const std::string& token, const char* driver_version = "1.0",
                              const char* firmware_version = "1.0",
                              hcr::HardwareClass hardware_class = hcr::HardwareClass::Gpu,
                              const char* family = "family.test") {
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::RegisterDevice;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.device = device_registration(token, driver_version, firmware_version, hardware_class, family);
    const hcr::ApplyResult result = registry.apply(publication);
    if (result.accepted()) {
      generations_[publication.device.identity.id] = result.device_generation;
      boots_[publication.device.identity.id] = result.device_boot;
    }
    return result;
  }

  /// Announces a new device generation (reset or reincarnation).
  hcr::ApplyResult reset_device(const std::string& token, const char* driver_version = "1.0",
                                const char* firmware_version = "1.0",
                                const char* incarnation_suffix = "2") {
    hcr::DeviceRegistration registration = device_registration(token, driver_version, firmware_version);
    registration.identity.incarnation = hcr::DeviceIncarnationId::parse(
        "inc.test." + hcr::ascii_lower(token) + "." + incarnation_suffix);
    registration.device_boot_observed = true;
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::PublishDeviceGeneration;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.device = registration;
    const hcr::ApplyResult result = registry.apply(publication);
    if (result.accepted()) {
      generations_[registration.identity.id] = result.device_generation;
      boots_[registration.identity.id] = result.device_boot;
    }
    return result;
  }

  hcr::ApplyResult publish(const std::string& token,
                           const char* capability,
                           hcr::SupportState support,
                           hcr::CapabilityValue value = hcr::CapabilityValue{},
                           hcr::PrecisionClass precision = hcr::PrecisionClass::DirectReported,
                           const char* reference = "test.reference",
                           hcr::SourceClass source_class = hcr::SourceClass::RuntimeProbe,
                           hcr::ProvenanceClass provenance = hcr::ProvenanceClass::RealRuntimeProbe,
                           hcr::ConditionPtr conditions = nullptr,
                           hcr::DeviceGeneration generation_override = hcr::DeviceGeneration{},
                           hcr::EvidenceGeneration evidence_override = hcr::EvidenceGeneration{}) {
    const hcr::DeviceId device = hcr::canonical_device_id("dev.test", token);
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::PublishCapability;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_override.valid() ? evidence_override : evidence_generation_;
    publication.capability.device = device;
    const auto known_generation = generations_.find(device);
    const auto known_boot = boots_.find(device);
    publication.capability.device_generation =
        generation_override.valid()
            ? generation_override
            : (known_generation == generations_.end() ? hcr::DeviceGeneration::first()
                                                      : known_generation->second);
    publication.capability.device_boot =
        known_boot == boots_.end() ? hcr::DeviceBootId::first() : known_boot->second;
    publication.capability.capability = hcr::CapabilityId::parse(capability);
    const hcr::CapabilitySchema* schema = hcr::find_builtin_schema_by_capability(publication.capability.capability);
    if (schema == nullptr) {
      hcr::ApplyResult missing;
      missing.code = hcr::ApplyCode::RejectedUnknownSchema;
      missing.detail = "fixture has no schema for this capability";
      return missing;
    }
    publication.capability.schema = schema->id;
    publication.capability.support = support;
    publication.capability.value = std::move(value);
    publication.capability.precision = precision;
    publication.capability.conditions = std::move(conditions);
    publication.capability.source.source_class = source_class;
    publication.capability.source.provenance = provenance;
    publication.capability.source.adapter = "test.fixture";
    publication.capability.source.native_reference = reference;
    publication.capability.source.observed_at_utc = "2026-01-01T00:00:00Z";
    publication.capability.source.evidence_generation = publication.evidence_generation;
    return registry.apply(publication);
  }

  hcr::ResolvedCapability query(const std::string& token, const char* capability,
                                const hcr::EnvironmentContext& environment = hcr::EnvironmentContext{}) const {
    hcr::CapabilityQuery query_request;
    query_request.device = hcr::canonical_device_id("dev.test", token);
    query_request.capability = hcr::CapabilityId::parse(capability);
    query_request.environment = environment;
    hcr::ResolvedCapability resolved;
    const hcr::Status status = registry.query_capability(query_request, resolved);
    HCR_REQUIRE(status.ok());
    return resolved;
  }

  [[nodiscard]] hcr::DeviceGeneration generation_of(const std::string& token) const {
    const auto it = generations_.find(hcr::canonical_device_id("dev.test", token));
    return it == generations_.end() ? hcr::DeviceGeneration{} : it->second;
  }

  [[nodiscard]] hcr::DeviceId device_of(const std::string& token) const {
    return hcr::canonical_device_id("dev.test", token);
  }

  hcr::ApplyResult publish_quirk(const char* quirk_id,
                                 const char* capability,
                                 hcr::QuirkSeverity severity,
                                 hcr::QuirkGeneration generation = hcr::QuirkGeneration::first(),
                                 const char* family = nullptr,
                                 const char* driver_minimum = nullptr,
                                 const char* driver_maximum = nullptr,
                                 hcr::QuirkCategory category = hcr::QuirkCategory::DriverDefect) {
    hcr::QuirkRecord quirk;
    quirk.id = hcr::QuirkId::parse(quirk_id);
    quirk.quirk_generation = generation;
    quirk.title = quirk_id;
    quirk.description = "test quirk";
    quirk.category = category;
    quirk.severity = severity;
    quirk.mitigation = hcr::MitigationKind::UpgradeDriver;
    if (family != nullptr) quirk.applicability.families.push_back(hcr::HardwareFamilyId::parse(family));
    if (driver_minimum != nullptr || driver_maximum != nullptr) {
      hcr::VersionRange range;
      if (driver_minimum != nullptr) range.minimum = hcr::SoftwareVersion::parse(driver_minimum);
      if (driver_maximum != nullptr) range.maximum = hcr::SoftwareVersion::parse(driver_maximum);
      quirk.applicability.driver_ranges.push_back(range);
    }
    quirk.impacted_capabilities.push_back(hcr::CapabilityId::parse(capability));
    quirk.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
    quirk.source.provenance = hcr::ProvenanceClass::CuratedStatic;
    quirk.source.publisher.id = publisher.id;
    quirk.source.adapter = "test.fixture";
    quirk.source.evidence_generation = evidence_generation_;

    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::PublishQuirk;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.quirk = quirk;
    return registry.apply(publication);
  }

  hcr::ApplyResult withdraw_quirk(const char* quirk_id,
                                  hcr::QuirkGeneration generation = hcr::QuirkGeneration::first()) {
    hcr::QuirkRecord quirk;
    quirk.id = hcr::QuirkId::parse(quirk_id);
    quirk.quirk_generation = generation;
    quirk.title = quirk_id;
    quirk.impacted_capabilities.push_back(hcr::CapabilityId::parse("cap.compute.fp32"));
    quirk.withdrawn = true;
    quirk.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
    quirk.source.provenance = hcr::ProvenanceClass::CuratedStatic;
    quirk.source.publisher.id = publisher.id;
    quirk.source.adapter = "test.fixture";
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::WithdrawQuirk;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.quirk = quirk;
    return registry.apply(publication);
  }

  hcr::ApplyResult publish_compatibility(const char* fact_id,
                                         hcr::HardwareReference lhs,
                                         hcr::HardwareReference rhs,
                                         hcr::CompatibilityOutcome outcome,
                                         const char* reason = "test reason",
                                         hcr::CompatibilityGeneration generation =
                                             hcr::CompatibilityGeneration::first()) {
    hcr::CompatibilityFact fact;
    fact.id = hcr::CompatibilityFactId::parse(fact_id);
    fact.compatibility_generation = generation;
    fact.kind = hcr::CompatibilitySubjectKind::AcceleratorRuntime;
    fact.lhs = std::move(lhs);
    fact.rhs = std::move(rhs);
    fact.outcome = outcome;
    fact.reason = reason;
    fact.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
    fact.source.provenance = hcr::ProvenanceClass::CuratedStatic;
    fact.source.publisher.id = publisher.id;
    fact.source.adapter = "test.fixture";
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::PublishCompatibility;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.compatibility = fact;
    return registry.apply(publication);
  }

  hcr::ApplyResult withdraw_capability(const std::string& token, const char* capability) {
    const hcr::DeviceId device = hcr::canonical_device_id("dev.test", token);
    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::WithdrawCapability;
    publication.publisher = publisher;
    publication.sequence = sequence_++;
    publication.evidence_generation = evidence_generation_;
    publication.capability.device = device;
    publication.capability.device_generation = generations_[device];
    publication.capability.device_boot = boots_[device];
    publication.capability.capability = hcr::CapabilityId::parse(capability);
    const hcr::CapabilitySchema* schema =
        hcr::find_builtin_schema_by_capability(publication.capability.capability);
    publication.capability.schema = schema->id;
    publication.capability.support = hcr::SupportState::Unknown;
    publication.capability.withdrawn = true;
    publication.capability.source.source_class = hcr::SourceClass::RuntimeProbe;
    publication.capability.source.provenance = hcr::ProvenanceClass::RealRuntimeProbe;
    publication.capability.source.adapter = "test.fixture";
    publication.capability.source.native_reference = "test.withdraw";
    publication.capability.source.evidence_generation = publication.evidence_generation;
    return registry.apply(publication);
  }

  [[nodiscard]] std::uint64_t next_sequence() { return sequence_++; }
  void set_evidence_generation(hcr::EvidenceGeneration generation) { evidence_generation_ = generation; }

 private:
  std::uint64_t sequence_ = 1;
  hcr::EvidenceGeneration evidence_generation_ = hcr::EvidenceGeneration::first();
  std::map<hcr::DeviceId, hcr::DeviceGeneration> generations_;
  std::map<hcr::DeviceId, hcr::DeviceBootId> boots_;
};

/// Convenience condition builders used across suites.
inline hcr::ConditionPtr min_driver(const char* version) {
  return std::make_shared<const hcr::ConditionNode>(hcr::ConditionKind::MinDriverVersion, "", version);
}

inline hcr::ConditionPtr required_os(const char* value) {
  return std::make_shared<const hcr::ConditionNode>(hcr::ConditionKind::RequiredOs, "", value);
}

inline hcr::ConditionPtr required_privilege(const char* value) {
  return std::make_shared<const hcr::ConditionNode>(hcr::ConditionKind::RequiredPrivilege, "", value);
}

inline hcr::ConditionPtr all_of(std::vector<hcr::ConditionPtr> children) {
  auto node = std::make_shared<hcr::ConditionNode>();
  node->kind = hcr::ConditionKind::AllOf;
  node->children = std::move(children);
  return node;
}

inline hcr::ConditionPtr any_of(std::vector<hcr::ConditionPtr> children) {
  auto node = std::make_shared<hcr::ConditionNode>();
  node->kind = hcr::ConditionKind::AnyOf;
  node->children = std::move(children);
  return node;
}

}  // namespace hcrtest
