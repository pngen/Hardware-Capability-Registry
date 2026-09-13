// Example 3: a capability that only holds under a condition.
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
  registration.device = example::device("accelerator-c", "540.0");
  const hcr::ApplyResult applied = registry.apply(registration);

  auto conditions = std::make_shared<hcr::ConditionNode>();
  conditions->kind = hcr::ConditionKind::AllOf;
  conditions->children.push_back(
      std::make_shared<const hcr::ConditionNode>(hcr::ConditionKind::MinDriverVersion, "", "550.0"));
  conditions->children.push_back(
      std::make_shared<const hcr::ConditionNode>(hcr::ConditionKind::RequiredPrivilege, "", "admin"));

  example::publish(registry, publisher, registration.device, applied.device_generation, applied.device_boot,
                   "cap.net.gpudirect", hcr::SupportState::SupportedConditional, hcr::CapabilityValue(true),
                   hcr::PrecisionClass::DirectReported, "vendorQuery(gpudirect)",
                   hcr::SourceClass::LiveVendorApi, hcr::ProvenanceClass::RealVendorApi, conditions, 2);

  const char* const labels[] = {"no environment supplied", "conditions satisfied", "condition unmet"};
  for (int index = 0; index < 3; ++index) {
    hcr::EnvironmentContext environment;
    if (index == 1) {
      environment.set_driver_version("580.0");
      environment.set_privilege("admin");
    } else if (index == 2) {
      environment.set_driver_version("540.0");
      environment.set_privilege("admin");
    }
    hcr::CapabilityQuery query;
    query.device = registration.device.identity.id;
    query.capability = example::capability("cap.net.gpudirect");
    query.environment = environment;
    hcr::ResolvedCapability resolved;
    if (!registry.query_capability(query, resolved).ok()) return 1;
    std::printf("%-24s -> %s\n", labels[index], hcr::render_capability_brief(resolved).c_str());
  }
  return 0;
}
