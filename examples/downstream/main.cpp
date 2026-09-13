// Independent downstream consumer of Hardware Capability Registry 1.0.0.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// This program is built against installed artifacts only. It demonstrates the
// contract a downstream system relies on: canonical hardware identity,
// generation-qualified capability answers, explicit UNKNOWN and UNSUPPORTED,
// conditional support, quirks, contradictory evidence, fencing and recovery.
#include <cstdio>
#include <string>
#include <vector>

#include <hcr/adapters.hpp>
#include <hcr/catalog.hpp>
#include <hcr/discovery.hpp>
#include <hcr/persistence.hpp>
#include <hcr/registry.hpp>
#include <hcr/render.hpp>
#include <hcr/time.hpp>
#include <hcr/version.hpp>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  std::printf("  %-58s %s\n", what, condition ? "ok" : "FAILED");
  if (!condition) ++failures;
}

hcr::CapabilityId capability(const char* name) { return hcr::CapabilityId::parse(name); }

}  // namespace

int main() {
  std::printf("downstream consumer against Hardware Capability Registry %s\n", hcr::version_string());

  hcr::CapabilityRegistry registry;
  hcr::PublisherIdentity identity;
  identity.id = hcr::PublisherId::parse("publisher.downstream");
  const hcr::ApplyResult registered = registry.register_publisher(identity, "downstream.consumer");
  expect(registered.accepted(), "register a publisher and receive a boot identity");
  const hcr::PublisherIdentity publisher = registered.assigned_publisher;

  hcr::DeviceRegistration registration;
  registration.identity.id = hcr::canonical_device_id("dev.downstream", "accelerator-1");
  registration.identity.incarnation = hcr::DeviceIncarnationId::parse("inc.downstream.1");
  registration.identity.hardware.vendor = hcr::VendorId::parse("vendor.downstream");
  registration.identity.hardware.product = hcr::ProductId::parse("product.downstream");
  registration.identity.hardware.family = hcr::HardwareFamilyId::parse("family.downstream");
  registration.identity.hardware.model = hcr::HardwareModelId::parse("model.downstream");
  registration.identity.hardware.revision = hcr::HardwareRevisionId::parse("rev.downstream.a0");
  registration.identity.hardware.architecture = hcr::ArchitectureId::parse("arch.downstream");
  registration.identity.hardware.hardware_class = hcr::HardwareClass::Gpu;
  registration.identity.platform = hcr::PlatformId::parse("platform.downstream");
  registration.identity.display_name = "downstream accelerator";
  registration.software.driver.id = hcr::DriverId::parse("driver.downstream");
  registration.software.driver.version = "540.0";
  registration.platform.id = hcr::PlatformId::parse("platform.downstream");
  registration.observed_at_utc = hcr::utc_now_iso8601();

  hcr::Publication publication;
  publication.kind = hcr::PublicationKind::RegisterDevice;
  publication.publisher = publisher;
  publication.sequence = 1;
  publication.evidence_generation = hcr::EvidenceGeneration::first();
  publication.device = registration;
  const hcr::ApplyResult applied = registry.apply(publication);
  expect(applied.accepted(), "register a hardware subject through the public API");

  const auto publish = [&](const char* name, hcr::SupportState support, hcr::CapabilityValue value,
                           hcr::PrecisionClass precision, const char* reference,
                           hcr::ConditionPtr conditions, std::uint64_t sequence) {
    hcr::Publication capability_publication;
    capability_publication.kind = hcr::PublicationKind::PublishCapability;
    capability_publication.publisher = publisher;
    capability_publication.sequence = sequence;
    capability_publication.evidence_generation = hcr::EvidenceGeneration::first();
    capability_publication.capability.device = registration.identity.id;
    capability_publication.capability.device_generation = applied.device_generation;
    capability_publication.capability.device_boot = applied.device_boot;
    capability_publication.capability.capability = capability(name);
    const hcr::CapabilitySchema* schema =
        hcr::find_builtin_schema_by_capability(capability_publication.capability.capability);
    if (schema == nullptr) return;
    capability_publication.capability.schema = schema->id;
    capability_publication.capability.support = support;
    capability_publication.capability.value = std::move(value);
    capability_publication.capability.precision = precision;
    capability_publication.capability.conditions = std::move(conditions);
    capability_publication.capability.source.source_class = hcr::SourceClass::LiveVendorApi;
    capability_publication.capability.source.provenance = hcr::ProvenanceClass::RealVendorApi;
    capability_publication.capability.source.adapter = "downstream.consumer";
    capability_publication.capability.source.native_reference = reference;
    registry.apply(capability_publication);
  };

  publish("cap.memory.capacity", hcr::SupportState::SupportedNative,
          hcr::CapabilityValue(hcr::BytesValue{34359738368ull}), hcr::PrecisionClass::Exact,
          "vendor.query(memory)", nullptr, 2);
  publish("cap.partition.mechanism", hcr::SupportState::Unsupported, hcr::CapabilityValue{},
          hcr::PrecisionClass::Exact, "vendor.query(partition) -> NOT_SUPPORTED", nullptr, 3);
  auto conditions = std::make_shared<hcr::ConditionNode>(hcr::ConditionKind::MinDriverVersion, "", "550.0");
  publish("cap.net.gpudirect", hcr::SupportState::SupportedConditional, hcr::CapabilityValue(true),
          hcr::PrecisionClass::DirectReported, "vendor.query(gpudirect)", conditions, 4);

  hcr::CapabilityQuery query;
  query.device = registration.identity.id;
  hcr::ResolvedCapability resolved;

  query.capability = capability("cap.memory.capacity");
  expect(registry.query_capability(query, resolved).ok() &&
             resolved.effective_support == hcr::EffectiveSupport::Supported,
         "query a supported capability with provenance");

  query.capability = capability("cap.partition.mechanism");
  expect(registry.query_capability(query, resolved).ok() &&
             resolved.truth == hcr::TruthClass::Unsupported,
         "UNSUPPORTED stays distinct from UNKNOWN");

  query.capability = capability("cap.net.rdma");
  expect(registry.query_capability(query, resolved).ok() &&
             resolved.effective_support == hcr::EffectiveSupport::Unknown,
         "a capability with no evidence answers UNKNOWN");

  query.capability = capability("cap.net.gpudirect");
  query.environment = hcr::EnvironmentContext{};
  expect(registry.query_capability(query, resolved).ok() &&
             resolved.effective_support == hcr::EffectiveSupport::SupportedConditionalUnknown,
         "conditional support is not flattened when the environment is unknown");
  query.environment.set_driver_version("560.0");
  expect(registry.query_capability(query, resolved).ok() &&
             resolved.effective_support == hcr::EffectiveSupport::Supported,
         "conditional support is satisfied by the environment it declares");

  const std::string path = "hcr-downstream-state.bin";
  {
    hcr::FileStateStore store(path);
    expect(store.save(registry.export_durable_state()).ok(), "persist durable state atomically");
    hcr::DurableState state;
    expect(store.load(state).ok(), "reload durable state with integrity checking");
    hcr::CapabilityRegistry restored;
    hcr::ImportReport report;
    expect(restored.import_durable_state(state, report).ok(), "import durable state into a fresh process");
    hcr::CapabilityQuery restored_query;
    restored_query.device = registration.identity.id;
    restored_query.capability = capability("cap.memory.capacity");
    hcr::ResolvedCapability after_recovery;
    expect(restored.query_capability(restored_query, after_recovery).ok() &&
               after_recovery.effective_support == hcr::EffectiveSupport::RevalidationRequired,
           "live evidence requires revalidation after a restart");
  }
  std::remove(path.c_str());

  hcr::RegistrySnapshot snapshot;
  expect(registry.snapshot(snapshot).ok() && snapshot.capabilities.size() >= 3,
         "produce an immutable snapshot with a content digest");

  const std::vector<hcr::CapabilitySchema>& catalog = hcr::builtin_capability_schemas();
  expect(catalog.size() > 90, "the typed capability catalog is available to consumers");

  std::printf("downstream consumer %s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
