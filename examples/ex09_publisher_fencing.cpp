// Example 9: publisher authority, fencing and reincarnation.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "example_support.hpp"

int main() {
  hcr::CapabilityRegistry registry;
  hcr::PublisherIdentity identity;
  identity.id = hcr::PublisherId::parse("publisher.fenced");
  const hcr::ApplyResult registered = registry.register_publisher(identity, "example");
  const hcr::PublisherIdentity publisher = registered.assigned_publisher;
  std::printf("publisher boot %llu assigned by the registry\n",
              static_cast<unsigned long long>(publisher.boot.value()));

  hcr::Publication registration;
  registration.kind = hcr::PublicationKind::RegisterDevice;
  registration.publisher = publisher;
  registration.sequence = 1;
  registration.evidence_generation = hcr::EvidenceGeneration::first();
  registration.device = example::device("accelerator-i");
  const hcr::ApplyResult applied = registry.apply(registration);
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.reliability.ecc_exposure", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(true), hcr::PrecisionClass::DirectReported, "vendorQuery(ecc)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 2);

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  query.capability = example::capability("cap.reliability.ecc_exposure");
  hcr::ResolvedCapability resolved;
  registry.query_capability(query, resolved);
  std::printf("before fencing: %s\n", hcr::render_capability_brief(resolved).c_str());

  // The process that produced the evidence is gone; its boot identity is
  // permanently retired and its evidence is no longer authoritative.
  registry.retire_publisher_boot(publisher.id, publisher.boot, "publisher process died");
  registry.query_capability(query, resolved);
  std::printf("after death:    %s\n", hcr::render_capability_brief(resolved).c_str());

  // A replacement process receives a fresh boot identity and must republish.
  const hcr::ApplyResult replacement = registry.register_publisher(identity, "example");
  std::printf("replacement boot %llu (old boot %llu)\n",
              static_cast<unsigned long long>(replacement.assigned_publisher.boot.value()),
              static_cast<unsigned long long>(publisher.boot.value()));
  example::publish(registry, replacement.assigned_publisher, registration.device, applied.device_generation,
                   applied.device_boot, "cap.reliability.ecc_exposure", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(true), hcr::PrecisionClass::DirectReported, "vendorQuery(ecc)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 1);
  registry.query_capability(query, resolved);
  std::printf("after republish: %s\n", hcr::render_capability_brief(resolved).c_str());
  return 0;
}
