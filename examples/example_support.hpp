// Hardware Capability Registry - shared example helpers.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdio>
#include <string>

#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"
#include "hcr/discovery.hpp"
#include "hcr/registry.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"

namespace example {

inline hcr::CapabilityId capability(const char* name) { return hcr::CapabilityId::parse(name); }

/// Registers a publisher and returns the identity the registry assigned.
inline hcr::PublisherIdentity register_publisher(hcr::CapabilityRegistry& registry, const char* id,
                                                 const char* adapter = "example") {
  hcr::PublisherIdentity identity;
  identity.id = hcr::PublisherId::parse(id);
  const hcr::ApplyResult result = registry.register_publisher(identity, adapter);
  if (!result.accepted()) {
    std::printf("publisher registration failed: %s\n", result.detail.c_str());
    return {};
  }
  return result.assigned_publisher;
}

/// Builds a device registration for a fictional device used by the examples.
inline hcr::DeviceRegistration device(const char* token, const char* driver_version = "1.0.0",
                                      const char* firmware_version = "1.0.0",
                                      hcr::HardwareClass hardware_class = hcr::HardwareClass::Gpu,
                                      const char* family = "family.example") {
  hcr::DeviceRegistration registration;
  registration.identity.id = hcr::canonical_device_id("dev.example", token);
  registration.identity.incarnation =
      hcr::DeviceIncarnationId::parse(std::string("inc.example.") + hcr::ascii_lower(token) + ".1");
  registration.identity.hardware.vendor = hcr::VendorId::parse("vendor.example");
  registration.identity.hardware.product = hcr::ProductId::parse(std::string("product.") + token);
  registration.identity.hardware.family = hcr::HardwareFamilyId::parse(family);
  registration.identity.hardware.model =
      hcr::HardwareModelId::parse(std::string("model.") + hcr::ascii_lower(token));
  registration.identity.hardware.revision = hcr::HardwareRevisionId::parse("rev.example.a0");
  registration.identity.hardware.architecture = hcr::ArchitectureId::parse("arch.example.sm_10");
  registration.identity.hardware.hardware_class = hardware_class;
  registration.identity.hardware.native_identifiers = {{"example.token", token}};
  registration.identity.platform = hcr::PlatformId::parse("platform.example");
  registration.identity.display_name = token;
  registration.software.driver.id = hcr::DriverId::parse("driver.example");
  registration.software.driver.version = driver_version;
  registration.software.firmware.id = hcr::FirmwareId::parse("fw.example");
  registration.software.firmware.version = firmware_version;
  registration.software.runtime.id = hcr::RuntimeId::parse("runtime.example");
  registration.software.runtime.version = "1.0";
  registration.platform.id = hcr::PlatformId::parse("platform.example");
  registration.platform.display_name = "example platform";
  registration.platform.vendor = "vendor.example";
  registration.platform.model = "model.example";
  registration.platform.architecture = hcr::ArchitectureId::parse("arch.example.sm_10");
  registration.observed_at_utc = hcr::utc_now_iso8601();
  return registration;
}

/// Publishes one capability through the production ingestion path.
inline hcr::ApplyResult publish(hcr::CapabilityRegistry& registry, const hcr::PublisherIdentity& publisher,
                                const hcr::DeviceRegistration& registration,
                                hcr::DeviceGeneration generation, hcr::DeviceBootId boot,
                                const char* capability_name, hcr::SupportState support,
                                hcr::CapabilityValue value = hcr::CapabilityValue{},
                                hcr::PrecisionClass precision = hcr::PrecisionClass::DirectReported,
                                const char* reference = "example.api",
                                hcr::SourceClass source_class = hcr::SourceClass::LiveVendorApi,
                                hcr::ProvenanceClass provenance = hcr::ProvenanceClass::RealVendorApi,
                                hcr::ConditionPtr conditions = nullptr,
                                std::uint64_t sequence = 0) {
  hcr::Publication publication;
  publication.kind = hcr::PublicationKind::PublishCapability;
  publication.publisher = publisher;
  publication.sequence = sequence;
  publication.evidence_generation = hcr::EvidenceGeneration::first();
  publication.capability.device = registration.identity.id;
  publication.capability.device_generation = generation;
  publication.capability.device_boot = boot;
  publication.capability.capability = capability(capability_name);
  const hcr::CapabilitySchema* schema = hcr::find_builtin_schema_by_capability(publication.capability.capability);
  if (schema == nullptr) return {};
  publication.capability.schema = schema->id;
  publication.capability.support = support;
  publication.capability.value = std::move(value);
  publication.capability.precision = precision;
  publication.capability.conditions = std::move(conditions);
  publication.capability.source.source_class = source_class;
  publication.capability.source.provenance = provenance;
  publication.capability.source.adapter = "example";
  publication.capability.source.native_reference = reference;
  publication.capability.source.observed_at_utc = hcr::utc_now_iso8601();
  publication.capability.source.evidence_generation = hcr::EvidenceGeneration::first();
  return registry.apply(publication);
}

inline void print(const hcr::ResolvedCapability& resolved) {
  std::printf("%s\n", hcr::render_capability_brief(resolved).c_str());
}

}  // namespace example
