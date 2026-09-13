// Example 5: a device reset opens a new generation and stales old evidence.
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
  registration.device = example::device("accelerator-e");
  const hcr::ApplyResult first = registry.apply(registration);
  example::publish(registry, publisher, registration.device, first.device_generation, first.device_boot,
                   "cap.runtime.ipc", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::Probed, "vendorProbe(ipc)",
                   hcr::SourceClass::RuntimeProbe, hcr::ProvenanceClass::RealRuntimeProbe, nullptr, 2);

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  query.capability = example::capability("cap.runtime.ipc");
  hcr::ResolvedCapability resolved;
  registry.query_capability(query, resolved);
  std::printf("before reset: %s\n", hcr::render_capability_brief(resolved).c_str());

  // The device resets: a new boot and a new capability generation boundary.
  hcr::Publication reset;
  reset.kind = hcr::PublicationKind::PublishDeviceGeneration;
  reset.publisher = publisher;
  reset.sequence = 3;
  reset.evidence_generation = hcr::EvidenceGeneration::first();
  reset.device = registration.device;
  reset.device.device_boot_observed = true;
  reset.device.identity.incarnation = hcr::DeviceIncarnationId::parse("inc.example.accelerator-e.2");
  const hcr::ApplyResult rebooted = registry.apply(reset);
  std::printf("device generation %llu -> %llu\n",
              static_cast<unsigned long long>(first.device_generation.value()),
              static_cast<unsigned long long>(rebooted.device_generation.value()));

  registry.query_capability(query, resolved);
  std::printf("after reset:  %s\n", hcr::render_capability_brief(resolved).c_str());

  // Evidence bound to the previous generation is refused outright.
  const hcr::ApplyResult replayed = example::publish(
      registry, publisher, registration.device, first.device_generation, first.device_boot, "cap.runtime.ipc",
      hcr::SupportState::SupportedNative, hcr::CapabilityValue(true), hcr::PrecisionClass::Probed,
      "vendorProbe(ipc)", hcr::SourceClass::RuntimeProbe, hcr::ProvenanceClass::RealRuntimeProbe, nullptr, 4);
  std::printf("replayed old-generation evidence: %s (%s)\n", hcr::to_string(replayed.code),
              replayed.detail.c_str());

  // Fresh evidence for the new generation becomes current again.
  example::publish(registry, publisher, registration.device, rebooted.device_generation, rebooted.device_boot,
                   "cap.runtime.ipc", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::Probed, "vendorProbe(ipc)",
                   hcr::SourceClass::RuntimeProbe, hcr::ProvenanceClass::RealRuntimeProbe, nullptr, 5);
  registry.query_capability(query, resolved);
  std::printf("after republish: %s\n", hcr::render_capability_brief(resolved).c_str());
  return 0;
}
