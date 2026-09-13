// Example 7: contradictory evidence is preserved and resolved explicitly.
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
  registration.device = example::device("accelerator-g", "470.57");
  const hcr::ApplyResult applied = registry.apply(registration);

  // A curated database says the capability is present.
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.fp8", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::Curated, "curated.architecture-table",
                   hcr::SourceClass::StaticCuratedDatabase, hcr::ProvenanceClass::CuratedStatic, nullptr, 2);
  // A live probe says it is not supported on this device.
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.fp8", hcr::SupportState::Unsupported, hcr::CapabilityValue{},
                   hcr::PrecisionClass::Exact, "runtimeProbe(fp8)",
                   hcr::SourceClass::RuntimeProbe, hcr::ProvenanceClass::RealRuntimeProbe, nullptr, 3);

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  query.capability = example::capability("cap.compute.fp8");
  hcr::ResolvedCapability resolved;
  if (!registry.query_capability(query, resolved).ok()) return 1;
  example::print(resolved);
  std::printf("contradiction state: %s\n", hcr::to_string(resolved.contradiction));
  std::printf("both records are retained and inspectable:\n");
  for (const hcr::EvidenceView& view : resolved.evidence) {
    std::printf("  %-24s %-22s %s%s\n", view.source.adapter.c_str(), hcr::to_string(view.source.provenance),
                hcr::to_string(view.support), view.winner ? "  [selected]" : "  [retained]");
  }
  return 0;
}
