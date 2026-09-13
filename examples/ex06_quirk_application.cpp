// Example 6: a quirk overrides advertised support until the driver changes.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "example_support.hpp"

namespace {

hcr::QuirkRecord make_quirk(const hcr::PublisherIdentity& publisher) {
  hcr::QuirkRecord quirk;
  quirk.id = hcr::QuirkId::parse("quirk.example.peer-defect");
  quirk.quirk_generation = hcr::QuirkGeneration::first();
  quirk.title = "peer transfers fail under this driver range";
  quirk.description = "The device advertises peer access; driver 530.0-539.99 produces transfer failures.";
  quirk.category = hcr::QuirkCategory::DriverDefect;
  quirk.severity = hcr::QuirkSeverity::Unusable;
  quirk.mitigation = hcr::MitigationKind::UpgradeDriver;
  quirk.applicability.families.push_back(hcr::HardwareFamilyId::parse("family.example"));
  hcr::VersionRange range;
  range.minimum = hcr::SoftwareVersion::parse("530.0");
  range.maximum = hcr::SoftwareVersion::parse("540.0");
  quirk.applicability.driver_ranges.push_back(range);
  quirk.impacted_capabilities.push_back(example::capability("cap.interconnect.peer_to_peer"));
  quirk.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
  quirk.source.provenance = hcr::ProvenanceClass::CuratedStatic;
  quirk.source.publisher.id = publisher.id;
  quirk.source.adapter = "example.curated";
  return quirk;
}

}  // namespace

int main() {
  hcr::CapabilityRegistry registry;
  const hcr::PublisherIdentity publisher = example::register_publisher(registry, "publisher.example");

  hcr::Publication registration;
  registration.kind = hcr::PublicationKind::RegisterDevice;
  registration.publisher = publisher;
  registration.sequence = 1;
  registration.evidence_generation = hcr::EvidenceGeneration::first();
  registration.device = example::device("accelerator-f", "535.104");
  const hcr::ApplyResult applied = registry.apply(registration);

  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.interconnect.peer_to_peer", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(true), hcr::PrecisionClass::DirectReported, "vendorQuery(peerAccess)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 2);

  hcr::Publication quirk_publication;
  quirk_publication.kind = hcr::PublicationKind::PublishQuirk;
  quirk_publication.publisher = publisher;
  quirk_publication.sequence = 3;
  quirk_publication.evidence_generation = hcr::EvidenceGeneration::first();
  quirk_publication.quirk = make_quirk(publisher);
  registry.apply(quirk_publication);

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  query.capability = example::capability("cap.interconnect.peer_to_peer");
  hcr::ResolvedCapability resolved;
  registry.query_capability(query, resolved);
  std::printf("with defective driver: %s\n", hcr::render_capability_brief(resolved).c_str());
  for (const hcr::QuirkApplication& application : resolved.quirks) {
    std::printf("  %s\n", hcr::render_quirk_application(application).c_str());
  }

  // Driver upgrade: the quirk no longer applies and the capability is usable.
  registration.device = example::device("accelerator-f", "570.0");
  registration.sequence = 4;
  registry.apply(registration);
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.interconnect.peer_to_peer", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(true), hcr::PrecisionClass::DirectReported, "vendorQuery(peerAccess)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 5);
  registry.query_capability(query, resolved);
  std::printf("after driver upgrade:  %s\n", hcr::render_capability_brief(resolved).c_str());
  return 0;
}
