// Example 1: register a hardware subject and query one capability.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "example_support.hpp"

int main() {
  hcr::CapabilityRegistry registry;
  const hcr::PublisherIdentity publisher = example::register_publisher(registry, "publisher.example");

  hcr::Publication registration;
  registration.kind = hcr::PublicationKind::RegisterDevice;
  registration.publisher = publisher;
  registration.sequence = 1;
  registration.evidence_generation = hcr::EvidenceGeneration::first();
  registration.device = example::device("accelerator-a");
  const hcr::ApplyResult applied = registry.apply(registration);
  if (!applied.accepted()) {
    std::printf("registration failed: %s\n", applied.detail.c_str());
    return 1;
  }

  const hcr::ApplyResult published = example::publish(
      registry, publisher, registration.device, applied.device_generation, applied.device_boot,
      "cap.memory.capacity", hcr::SupportState::SupportedNative,
      hcr::CapabilityValue(hcr::BytesValue{85899345920ull}), hcr::PrecisionClass::Exact,
      "example.deviceQuery", hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 2);
  if (!published.accepted()) {
    std::printf("publication failed: %s\n", published.detail.c_str());
    return 1;
  }

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  query.capability = example::capability("cap.memory.capacity");
  hcr::ResolvedCapability resolved;
  if (!registry.query_capability(query, resolved).ok()) return 1;
  example::print(resolved);
  std::printf("%s\n", resolved.explanation.c_str());

  // A capability nobody has published evidence for is UNKNOWN, not unsupported.
  query.capability = example::capability("cap.compute.fp8");
  if (!registry.query_capability(query, resolved).ok()) return 1;
  example::print(resolved);
  return 0;
}
