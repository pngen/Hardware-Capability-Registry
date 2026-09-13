// Example 8: compare two hardware subjects factually.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "example_support.hpp"

int main() {
  hcr::CapabilityRegistry registry;
  const hcr::PublisherIdentity publisher = example::register_publisher(registry, "publisher.example");

  const char* const tokens[] = {"accelerator-h1", "accelerator-h2"};
  std::vector<hcr::DeviceRegistration> devices;
  std::vector<hcr::ApplyResult> results;
  std::uint64_t sequence = 1;
  for (const char* token : tokens) {
    hcr::Publication registration;
    registration.kind = hcr::PublicationKind::RegisterDevice;
    registration.publisher = publisher;
    registration.sequence = sequence++;
    registration.evidence_generation = hcr::EvidenceGeneration::first();
    registration.device = example::device(token, token[13] == '1' ? "550.0" : "570.0");
    devices.push_back(registration.device);
    results.push_back(registry.apply(registration));
  }

  example::publish(registry, publisher, devices[0], results[0].device_generation, results[0].device_boot,
                   "cap.memory.capacity", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(hcr::BytesValue{85899345920ull}), hcr::PrecisionClass::Exact,
                   "vendorQuery(memory)", hcr::SourceClass::LiveVendorApi,
                   hcr::ProvenanceClass::RealVendorApi, nullptr, sequence++);
  example::publish(registry, publisher, devices[1], results[1].device_generation, results[1].device_boot,
                   "cap.memory.capacity", hcr::SupportState::SupportedNative,
                   hcr::CapabilityValue(hcr::BytesValue{42949672960ull}), hcr::PrecisionClass::Exact,
                   "vendorQuery(memory)", hcr::SourceClass::LiveVendorApi,
                   hcr::ProvenanceClass::RealVendorApi, nullptr, sequence++);
  example::publish(registry, publisher, devices[0], results[0].device_generation, results[0].device_boot,
                   "cap.compute.fp64", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::DirectReported, "vendorQuery(fp64)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, nullptr, sequence++);

  hcr::ComparisonRequest request;
  request.left = devices[0].identity.id;
  request.right = devices[1].identity.id;
  hcr::DeviceComparison comparison;
  if (!registry.compare_devices(request, comparison).ok()) return 1;
  std::printf("%s\n", hcr::render_comparison(comparison).c_str());
  return 0;
}
