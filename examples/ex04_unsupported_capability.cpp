// Example 4: positive evidence of non-support stays distinct from UNKNOWN.
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
  registration.device = example::device("accelerator-d", "616.92");
  const hcr::ApplyResult applied = registry.apply(registration);

  // The vendor API answered "not supported" for this exact device generation.
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.partition.mechanism", hcr::SupportState::Unsupported, hcr::CapabilityValue{},
                   hcr::PrecisionClass::Exact, "vendorQuery(partitionMode) -> NOT_SUPPORTED",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 2);

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  hcr::ResolvedCapability resolved;

  query.capability = example::capability("cap.partition.mechanism");
  if (!registry.query_capability(query, resolved).ok()) return 1;
  std::printf("positively unsupported: %s\n", hcr::render_capability_brief(resolved).c_str());
  std::printf("  truth class: %s\n", hcr::to_string(resolved.truth));

  query.capability = example::capability("cap.net.rdma");
  if (!registry.query_capability(query, resolved).ok()) return 1;
  std::printf("never observed:         %s\n", hcr::render_capability_brief(resolved).c_str());
  std::printf("  truth class: %s\n", hcr::to_string(resolved.truth));
  return 0;
}
