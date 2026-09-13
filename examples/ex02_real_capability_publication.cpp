// Example 2: publish and query a real capability observation.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "example_support.hpp"

int main() {
  hcr::CapabilityRegistry registry;
  const hcr::PublisherIdentity publisher =
      example::register_publisher(registry, "publisher.example", "example.live-vendor-api");

  hcr::Publication registration;
  registration.kind = hcr::PublicationKind::RegisterDevice;
  registration.publisher = publisher;
  registration.sequence = 1;
  registration.evidence_generation = hcr::EvidenceGeneration::first();
  registration.device = example::device("accelerator-b", "580.65.06", "96.00.74.00.01");
  const hcr::ApplyResult applied = registry.apply(registration);

  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.fp32", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::DirectReported, "vendorQuery(fp32)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 2);
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.datatypes", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(hcr::FeatureSetValue{{"bf16", "fp16", "fp8", "int8"}}),
                   hcr::PrecisionClass::DirectReported, "vendorQuery(datatypes)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, 3);

  hcr::DeviceCapabilityQuery query;
  query.device = registration.device.identity.id;
  query.only_current = true;
  std::vector<hcr::ResolvedCapability> capabilities;
  if (!registry.query_capabilities(query, capabilities).ok()) return 1;
  for (const hcr::ResolvedCapability& capability : capabilities) example::print(capability);
  std::printf("evidence records retained: %zu\n", capabilities.empty() ? 0u : capabilities.front().evidence.size());
  return 0;
}
