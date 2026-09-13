// Example 11: a deterministic synthetic fleet through the real pipeline.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "example_support.hpp"
#include "hcr/adapters.hpp"

int main() {
  hcr::CapabilityRegistry registry;
  hcr::LocalEvidenceSink sink(registry);

  hcr::DiscoveryContext context;
  context.publisher.id = hcr::PublisherId::parse("publisher.synthetic-example");
  context.platform.id = hcr::PlatformId::parse("platform.example");
  context.platform.display_name = "example host";
  context.platform.vendor = "vendor.example";
  context.platform.model = "model.example";
  context.platform.architecture = hcr::ArchitectureId::parse("arch.example");
  context.observed_at_utc = hcr::utc_now_iso8601();

  std::vector<hcr::SyntheticProfile> profiles{hcr::SyntheticProfile::MixedVendorFleet,
                                              hcr::SyntheticProfile::DpuSmartNicFabric,
                                              hcr::SyntheticProfile::CxlFabric,
                                              hcr::SyntheticProfile::ContradictionScenario};
  for (const hcr::SyntheticProfile profile : profiles) {
    auto backend = hcr::adapters::make_synthetic_backend(profile);
    hcr::DiscoveryReport report;
    hcr::run_discovery(*backend, context, sink, report);
    std::printf("%-24s accepted=%zu rejected=%zu\n", std::string(hcr::to_string(profile)).c_str(),
                report.accepted, report.rejected);
  }

  hcr::RegistrySnapshot snapshot;
  if (!registry.snapshot(snapshot).ok()) return 1;
  std::printf("%s\n", hcr::render_snapshot_summary(snapshot).c_str());

  hcr::FleetCapabilityQuery fleet;
  fleet.capability = example::capability("cap.net.rdma");
  std::vector<hcr::ResolvedCapability> devices;
  registry.query_fleet(fleet, devices);
  std::printf("devices reporting RDMA capability: %zu\n", devices.size());
  for (const hcr::ResolvedCapability& capability : devices) {
    std::printf("  %s -> %s (%s)\n", capability.device.str().c_str(),
                hcr::to_string(capability.effective_support), hcr::to_string(capability.truth));
  }
  return 0;
}
