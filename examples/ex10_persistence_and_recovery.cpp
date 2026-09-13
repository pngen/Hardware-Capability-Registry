// Example 10: persistence, restart and conservative recovery.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <cstdio>

#include "example_support.hpp"
#include "hcr/persistence.hpp"

int main() {
  const std::string path = "hcr-example-state.bin";
  hcr::CapabilityRegistry registry;
  const hcr::PublisherIdentity publisher = example::register_publisher(registry, "publisher.example");

  hcr::Publication registration;
  registration.kind = hcr::PublicationKind::RegisterDevice;
  registration.publisher = publisher;
  registration.sequence = 1;
  registration.evidence_generation = hcr::EvidenceGeneration::first();
  registration.device = example::device("accelerator-j");
  const hcr::ApplyResult applied = registry.apply(registration);

  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.fp32", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::Probed, "runtimeProbe(fp32)",
                   hcr::SourceClass::HardwareProbe, hcr::ProvenanceClass::RealLiveHardware, nullptr, 2);
  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.compute.bf16", hcr::SupportState::SupportedNative, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::Curated, "curated.architecture-table",
                   hcr::SourceClass::StaticCuratedDatabase, hcr::ProvenanceClass::CuratedStatic, nullptr, 3);

  hcr::FileStateStore store(path);
  const hcr::Status saved = store.save(registry.export_durable_state());
  if (!saved.ok()) {
    std::printf("save failed: %s\n", saved.describe().c_str());
    return 1;
  }

  // A fresh process loads the image. Live evidence must not silently become
  // current again; curated knowledge is preserved.
  hcr::CapabilityRegistry restored;
  hcr::DurableState state;
  if (!store.load(state).ok()) return 1;
  hcr::ImportReport report;
  if (!restored.import_durable_state(state, report).ok()) return 1;
  std::printf("%s\n", hcr::render_import_report(report).c_str());

  hcr::CapabilityQuery query;
  query.device = registration.device.identity.id;
  hcr::ResolvedCapability resolved;
  query.capability = example::capability("cap.compute.fp32");
  restored.query_capability(query, resolved);
  std::printf("live evidence after restart:    %s\n", hcr::render_capability_brief(resolved).c_str());
  query.capability = example::capability("cap.compute.bf16");
  restored.query_capability(query, resolved);
  std::printf("curated evidence after restart: %s\n", hcr::render_capability_brief(resolved).c_str());

  std::remove(path.c_str());
  return 0;
}
