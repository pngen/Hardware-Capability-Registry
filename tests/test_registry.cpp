// Hardware Capability Registry - registry behaviour tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <set>

#include "fixtures.hpp"
#include "harness.hpp"

using namespace hcr;
using namespace hcrtest;

HCR_TEST(publisher, registration_assigns_boot_and_new_boot_fences_the_old) {
  CapabilityRegistry registry;
  PublisherIdentity identity;
  identity.id = PublisherId::parse("publisher.a");
  const ApplyResult first = registry.register_publisher(identity, "test");
  HCR_REQUIRE(first.accepted());
  HCR_CHECK(first.assigned_publisher.boot == PublisherBootId::first());

  // Reconnecting with the same boot identity is idempotent.
  const ApplyResult again = registry.register_publisher(first.assigned_publisher, "test");
  HCR_CHECK(again.accepted());
  HCR_CHECK(again.assigned_publisher.boot == first.assigned_publisher.boot);

  // A fresh process (boot 0) receives a new boot and the old boot is fenced.
  const ApplyResult replacement = registry.register_publisher(identity, "test");
  HCR_REQUIRE(replacement.accepted());
  HCR_CHECK(replacement.assigned_publisher.boot > first.assigned_publisher.boot);

  PublisherRecord record;
  HCR_CHECK_STATUS(registry.find_publisher_record(identity.id, record));
  HCR_CHECK(record.boot_history >= 2u);
}

HCR_TEST(publisher, fenced_publisher_is_permanently_unauthorised) {
  Fixture fixture;
  const ApplyResult fenced = fixture.registry.fence_publisher(fixture.publisher.id, "test fence");
  HCR_REQUIRE(fenced.accepted());
  HCR_CHECK_EQ(fixture.add_device("fenced").code, ApplyCode::RejectedFencedPublisher);
  HCR_CHECK_EQ(fixture.registry.fence_publisher(fixture.publisher.id, "again").code,
               ApplyCode::AcceptedIdempotent);
  PublisherRecord record;
  HCR_CHECK_STATUS(fixture.registry.find_publisher_record(fixture.publisher.id, record));
  HCR_CHECK(record.state == PublisherState::Fenced);
}

HCR_TEST(device, registration_assigns_generation_one_and_boot_one) {
  Fixture fixture;
  const ApplyResult registered = fixture.add_device("gpu-a");
  HCR_REQUIRE(registered.accepted());
  HCR_CHECK(registered.device_generation == DeviceGeneration::first());
  HCR_CHECK(registered.device_boot == DeviceBootId::first());

  SubjectSnapshot subject;
  HCR_CHECK_STATUS(fixture.registry.find_device(fixture.device_of("gpu-a"), subject));
  HCR_CHECK(subject.generation == DeviceGeneration::first());
  HCR_CHECK(subject.identity.hardware.vendor == VendorId::parse("vendor.test"));
}

HCR_TEST(capability, publish_then_query_returns_provenance_backed_answer) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-b").accepted());
  HCR_REQUIRE(fixture.publish("gpu-b", "cap.memory.capacity", SupportState::SupportedNative,
                              CapabilityValue(BytesValue{1024}), PrecisionClass::DirectReported)
                  .accepted());

  const ResolvedCapability resolved = fixture.query("gpu-b", "cap.memory.capacity");
  HCR_CHECK(resolved.declared_support == SupportState::SupportedNative);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(resolved.truth == TruthClass::Real);
  HCR_CHECK(resolved.contradiction == ContradictionState::Consistent);
  HCR_CHECK(resolved.current);
  HCR_CHECK_EQ(resolved.value.describe(), std::string("1024 B"));
  HCR_REQUIRE(resolved.evidence.size() == 1u);
  HCR_CHECK(resolved.evidence.front().winner);
  HCR_CHECK(resolved.evidence.front().source.source_class == SourceClass::RuntimeProbe);
  HCR_CHECK(!resolved.explanation.empty());
  HCR_CHECK(resolved.usable());
}

HCR_TEST(capability, unknown_device_and_unknown_schema_are_rejected) {
  Fixture fixture;
  const ApplyResult missing_device =
      fixture.publish("absent", "cap.compute.fp32", SupportState::SupportedNative, CapabilityValue(true));
  HCR_CHECK_EQ(missing_device.code, ApplyCode::RejectedUnknownDevice);

  DeviceRegistration registration = fixture.device_registration("gpu-c");
  HCR_REQUIRE(fixture.add_device("gpu-c").accepted());
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher = fixture.publisher;
  publication.sequence = fixture.next_sequence();
  publication.evidence_generation = EvidenceGeneration::first();
  publication.capability.device = registration.identity.id;
  publication.capability.device_generation = DeviceGeneration::first();
  publication.capability.device_boot = DeviceBootId::first();
  publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v99");
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.source.source_class = SourceClass::RuntimeProbe;
  publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
  publication.capability.source.adapter = "test.fixture";
  const ApplyResult unknown_schema = fixture.registry.apply(publication);
  HCR_CHECK_MSG(unknown_schema.code == ApplyCode::RejectedUnknownSchema,
                std::string(to_string(unknown_schema.code)) + ": " + unknown_schema.detail);
}

HCR_TEST(capability, value_kind_must_match_the_schema) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-d").accepted());
  // cap.memory.capacity is a byte value; publishing a boolean is a type error.
  const ApplyResult wrong = fixture.publish("gpu-d", "cap.memory.capacity", SupportState::SupportedNative,
                                            CapabilityValue(true));
  HCR_CHECK_EQ(wrong.code, ApplyCode::RejectedValidation);
}

HCR_TEST(capability, a_capability_may_not_be_reinterpreted_by_another_schema) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-e").accepted());
  HCR_REQUIRE(fixture.publish("gpu-e", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher = fixture.publisher;
  publication.sequence = fixture.next_sequence();
  publication.evidence_generation = EvidenceGeneration::first();
  publication.capability.device = fixture.device_of("gpu-e");
  publication.capability.device_generation = fixture.generation_of("gpu-e");
  publication.capability.device_boot = DeviceBootId::first();
  publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp16.v1");
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.source.source_class = SourceClass::RuntimeProbe;
  publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
  publication.capability.source.adapter = "test.fixture";
  HCR_CHECK_EQ(fixture.registry.apply(publication).code, ApplyCode::RejectedUnknownSchema);
}

HCR_TEST(capability, stale_device_generation_and_boot_are_rejected) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-f").accepted());
  const DeviceGeneration old_generation = fixture.generation_of("gpu-f");
  HCR_REQUIRE(fixture.reset_device("gpu-f").accepted());
  HCR_CHECK(fixture.generation_of("gpu-f") > old_generation);
  const ApplyResult stale = fixture.publish("gpu-f", "cap.compute.fp32", SupportState::SupportedNative,
                                            CapabilityValue(true), PrecisionClass::DirectReported,
                                            "test", SourceClass::RuntimeProbe,
                                            ProvenanceClass::RealRuntimeProbe, nullptr, old_generation);
  HCR_CHECK_EQ(stale.code, ApplyCode::RejectedStaleDeviceGeneration);
}

HCR_TEST(capability, generation_rollback_is_rejected) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-g").accepted());
  HCR_REQUIRE(fixture.publish("gpu-g", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  const ResolvedCapability first = fixture.query("gpu-g", "cap.compute.fp32");
  HCR_REQUIRE(first.capability_generation.valid());

  const ApplyResult rollback =
      fixture.publish("gpu-g", "cap.compute.fp32", SupportState::Unsupported, CapabilityValue{},
                      PrecisionClass::Exact, "test", SourceClass::RuntimeProbe,
                      ProvenanceClass::RealRuntimeProbe, nullptr, DeviceGeneration{},
                      EvidenceGeneration::first());
  HCR_CHECK(rollback.accepted());
  const ResolvedCapability second = fixture.query("gpu-g", "cap.compute.fp32");
  HCR_CHECK(second.capability_generation > first.capability_generation);

  // An explicit publication cannot state a capability generation at or below
  // the current one.
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher = fixture.publisher;
  publication.sequence = fixture.next_sequence();
  publication.evidence_generation = EvidenceGeneration::first();
  publication.capability.device = fixture.device_of("gpu-g");
  publication.capability.device_generation = fixture.generation_of("gpu-g");
  publication.capability.device_boot = DeviceBootId::first();
  publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v1");
  publication.capability.capability_generation = first.capability_generation;
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.source.source_class = SourceClass::RuntimeProbe;
  publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
  publication.capability.source.adapter = "test.fixture";
  HCR_CHECK_EQ(fixture.registry.apply(publication).code, ApplyCode::RejectedStaleCapabilityGeneration);
}

HCR_TEST(capability, unknown_is_not_unsupported) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-h").accepted());
  HCR_REQUIRE(fixture.publish("gpu-h", "cap.net.rdma", SupportState::Unknown, CapabilityValue{},
                              PrecisionClass::Unknown, "no evidence")
                  .accepted());
  ResolvedCapability resolved = fixture.query("gpu-h", "cap.net.rdma");
  HCR_CHECK(resolved.declared_support == SupportState::Unknown);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Unknown);
  HCR_CHECK(resolved.truth == TruthClass::Unknown);
  HCR_CHECK(!resolved.usable());

  HCR_REQUIRE(fixture.publish("gpu-h", "cap.net.rdma", SupportState::Unsupported, CapabilityValue{},
                              PrecisionClass::Exact, "positive non-support")
                  .accepted());
  resolved = fixture.query("gpu-h", "cap.net.rdma");
  HCR_CHECK(resolved.declared_support == SupportState::Unsupported);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Unsupported);
  HCR_CHECK(resolved.truth == TruthClass::Unsupported);
}

HCR_TEST(capability, absent_evidence_is_unknown_not_unsupported) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-i").accepted());
  const ResolvedCapability resolved = fixture.query("gpu-i", "cap.compute.fp8");
  HCR_CHECK(!resolved.known);
  HCR_CHECK(resolved.declared_support == SupportState::Unknown);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Unknown);
  HCR_CHECK(resolved.truth == TruthClass::Unknown);
  HCR_CHECK_EQ(resolved.contradiction, ContradictionState::Consistent);
}

HCR_TEST(contradiction, live_evidence_is_preferred_and_both_records_are_preserved) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-j").accepted());
  HCR_REQUIRE(fixture.publish("gpu-j", "cap.compute.fp8", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Curated, "curated.table",
                              SourceClass::StaticCuratedDatabase, ProvenanceClass::CuratedStatic)
                  .accepted());
  HCR_REQUIRE(fixture.publish("gpu-j", "cap.compute.fp8", SupportState::Unsupported, CapabilityValue{},
                              PrecisionClass::Exact, "runtime.probe", SourceClass::RuntimeProbe,
                              ProvenanceClass::RealRuntimeProbe)
                  .accepted());

  const ResolvedCapability resolved = fixture.query("gpu-j", "cap.compute.fp8");
  HCR_CHECK(resolved.declared_support == SupportState::Unsupported);
  HCR_CHECK(resolved.contradiction == ContradictionState::PreferredLiveEvidence);
  HCR_CHECK(resolved.truth == TruthClass::Unsupported);
  HCR_REQUIRE(resolved.evidence.size() == 2u);
  std::size_t winners = 0;
  std::size_t curated = 0;
  for (const EvidenceView& view : resolved.evidence) {
    if (view.winner) ++winners;
    if (view.source.provenance == ProvenanceClass::CuratedStatic) ++curated;
  }
  HCR_CHECK_EQ(winners, std::size_t{1});
  HCR_CHECK_EQ(curated, std::size_t{1});
  HCR_CHECK(resolved.explanation.find("not-selected") != std::string::npos);
}

HCR_TEST(contradiction, higher_authority_source_wins_when_both_are_live) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-k").accepted());
  HCR_REQUIRE(fixture.publish("gpu-k", "cap.compute.fp16", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::DirectReported, "driver.query",
                              SourceClass::DriverQuery, ProvenanceClass::RealRuntimeProbe)
                  .accepted());
  HCR_REQUIRE(fixture.publish("gpu-k", "cap.compute.fp16", SupportState::Unsupported, CapabilityValue{},
                              PrecisionClass::Probed, "hardware.probe", SourceClass::HardwareProbe,
                              ProvenanceClass::RealLiveHardware)
                  .accepted());
  const ResolvedCapability resolved = fixture.query("gpu-k", "cap.compute.fp16");
  HCR_CHECK(resolved.declared_support == SupportState::Unsupported);
  HCR_CHECK(resolved.contradiction == ContradictionState::PreferredHigherAuthoritySource);
}

HCR_TEST(contradiction, equal_authority_disagreement_is_ambiguous) {
  CapabilityRegistry registry;
  const auto make_publisher = [&registry](const char* id) {
    PublisherIdentity identity;
    identity.id = PublisherId::parse(id);
    const ApplyResult result = registry.register_publisher(identity, "test");
    HCR_REQUIRE(result.accepted());
    return result.assigned_publisher;
  };
  const PublisherIdentity first = make_publisher("publisher.left");
  const PublisherIdentity second = make_publisher("publisher.right");

  DeviceRegistration registration;
  registration.identity.id = DeviceId::parse("dev.ambiguous");
  registration.identity.incarnation = DeviceIncarnationId::parse("inc.ambiguous.1");
  registration.identity.hardware.vendor = VendorId::parse("vendor.test");
  registration.identity.hardware.family = HardwareFamilyId::parse("family.test");
  registration.identity.hardware.model = HardwareModelId::parse("model.test");
  registration.identity.hardware.architecture = ArchitectureId::parse("arch.test");
  registration.identity.hardware.hardware_class = HardwareClass::Gpu;
  registration.identity.platform = PlatformId::parse("platform.test");
  registration.identity.display_name = "ambiguous";
  registration.software.driver.id = DriverId::parse("driver.test");
  registration.software.driver.version = "1.0";
  registration.platform.id = PlatformId::parse("platform.test");

  Publication registration_publication;
  registration_publication.kind = PublicationKind::RegisterDevice;
  registration_publication.publisher = first;
  registration_publication.sequence = 1;
  registration_publication.evidence_generation = EvidenceGeneration::first();
  registration_publication.device = registration;
  HCR_REQUIRE(registry.apply(registration_publication).accepted());

  const auto publish_from = [&](const PublisherIdentity& publisher, std::uint64_t sequence,
                                SupportState support) {
    Publication publication;
    publication.kind = PublicationKind::PublishCapability;
    publication.publisher = publisher;
    publication.sequence = sequence;
    publication.evidence_generation = EvidenceGeneration::first();
    publication.capability.device = registration.identity.id;
    publication.capability.device_generation = DeviceGeneration::first();
    publication.capability.device_boot = DeviceBootId::first();
    publication.capability.capability = CapabilityId::parse("cap.compute.tf32");
    publication.capability.schema = CapabilitySchemaId::parse("cap.compute.tf32.v1");
    publication.capability.support = support;
    if (asserts_capability_present(support)) publication.capability.value = CapabilityValue(true);
    publication.capability.precision = PrecisionClass::DirectReported;
    publication.capability.source.source_class = SourceClass::RuntimeProbe;
    publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
    publication.capability.source.adapter = "test";
    publication.capability.source.native_reference = publisher.id.str();
    publication.capability.source.evidence_generation = EvidenceGeneration::first();
    return registry.apply(publication);
  };
  HCR_REQUIRE(publish_from(first, 2, SupportState::SupportedNative).accepted());
  HCR_REQUIRE(publish_from(second, 1, SupportState::Unsupported).accepted());

  CapabilityQuery query;
  query.device = registration.identity.id;
  query.capability = CapabilityId::parse("cap.compute.tf32");
  ResolvedCapability resolved;
  HCR_CHECK_STATUS(registry.query_capability(query, resolved));
  HCR_CHECK_EQ(resolved.contradiction, ContradictionState::Ambiguous);
  HCR_CHECK_EQ(resolved.evidence.size(), std::size_t{2});
}

HCR_TEST(ordering, duplicate_sequence_conflict_and_regression_are_rejected) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-l").accepted());
  const ApplyResult accepted = fixture.publish("gpu-l", "cap.compute.int8", SupportState::SupportedNative,
                                               CapabilityValue(true));
  HCR_REQUIRE(accepted.accepted());

  // Replaying the identical publication is idempotent.
  Publication replay;
  replay.kind = PublicationKind::PublishCapability;
  replay.publisher = fixture.publisher;
  replay.sequence = 2;
  replay.evidence_generation = EvidenceGeneration::first();
  replay.capability.device = fixture.device_of("gpu-l");
  replay.capability.device_generation = fixture.generation_of("gpu-l");
  replay.capability.device_boot = DeviceBootId::first();
  replay.capability.capability = CapabilityId::parse("cap.compute.int8");
  replay.capability.schema = CapabilitySchemaId::parse("cap.compute.int8.v1");
  replay.capability.support = SupportState::SupportedNative;
  replay.capability.value = CapabilityValue(true);
  replay.capability.precision = PrecisionClass::DirectReported;
  replay.capability.source.source_class = SourceClass::RuntimeProbe;
  replay.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
  replay.capability.source.adapter = "test.fixture";
  replay.capability.source.native_reference = "test.reference";
  HCR_CHECK_EQ(fixture.registry.apply(replay).code, ApplyCode::AcceptedIdempotent);

  // The same sequence with different content is a conflicting duplicate.
  Publication conflicting = replay;
  conflicting.capability.support = SupportState::Unsupported;
  conflicting.capability.value = CapabilityValue{};
  HCR_CHECK_EQ(fixture.registry.apply(conflicting).code, ApplyCode::RejectedDuplicateConflict);

  // A lower sequence is stale.
  Publication stale = replay;
  stale.sequence = 1;
  HCR_CHECK_EQ(fixture.registry.apply(stale).code, ApplyCode::RejectedStaleSequence);

  // An evidence generation that regresses is rejected.
  Publication regression = replay;
  regression.sequence = fixture.next_sequence();
  regression.evidence_generation = EvidenceGeneration::first();
  regression.capability.source.evidence_generation = EvidenceGeneration::first();
  HCR_REQUIRE(fixture.registry.apply(regression).accepted());
  Publication backwards = replay;
  backwards.sequence = fixture.next_sequence();
  backwards.evidence_generation = EvidenceGeneration{};
  backwards.capability.source.evidence_generation = EvidenceGeneration{};
  HCR_CHECK_EQ(fixture.registry.apply(backwards).code, ApplyCode::RejectedGenerationRegression);
}

HCR_TEST(ordering, stale_boot_and_epoch_are_rejected_before_mutation) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-m").accepted());
  const std::uint64_t revision_before = fixture.registry.state_revision();

  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher = fixture.publisher;
  publication.publisher.boot = PublisherBootId{99};
  publication.sequence = fixture.next_sequence();
  publication.evidence_generation = EvidenceGeneration::first();
  publication.capability.device = fixture.device_of("gpu-m");
  publication.capability.device_generation = fixture.generation_of("gpu-m");
  publication.capability.device_boot = DeviceBootId::first();
  publication.capability.capability = CapabilityId::parse("cap.compute.int4");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.int4.v1");
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.source.source_class = SourceClass::RuntimeProbe;
  publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
  publication.capability.source.adapter = "test.fixture";
  HCR_CHECK_EQ(fixture.registry.apply(publication).code, ApplyCode::RejectedStaleBoot);
  HCR_CHECK_EQ(fixture.registry.state_revision(), revision_before);

  publication.publisher.boot = fixture.publisher.boot;
  publication.publisher.epoch = CoordinatorEpoch{77};
  HCR_CHECK_EQ(fixture.registry.apply(publication).code, ApplyCode::RejectedStaleEpoch);
  HCR_CHECK_EQ(fixture.registry.state_revision(), revision_before);

  CoordinatorEpoch advanced;
  HCR_CHECK_STATUS(fixture.registry.advance_coordinator_epoch(advanced));
  HCR_CHECK(advanced > fixture.publisher.epoch);
  publication.publisher.epoch = fixture.publisher.epoch;
  publication.sequence = fixture.next_sequence();
  HCR_CHECK_EQ(fixture.registry.apply(publication).code, ApplyCode::RejectedStaleEpoch);
}

HCR_TEST(reset, device_reset_stales_evidence_and_requires_fresh_publication) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-n").accepted());
  HCR_REQUIRE(fixture.publish("gpu-n", "cap.memory.capacity", SupportState::SupportedNative,
                              CapabilityValue(BytesValue{4096}))
                  .accepted());
  HCR_CHECK(fixture.query("gpu-n", "cap.memory.capacity").usable());

  const DeviceGeneration before = fixture.generation_of("gpu-n");
  HCR_REQUIRE(fixture.reset_device("gpu-n").accepted());
  HCR_CHECK(fixture.generation_of("gpu-n") > before);

  const ResolvedCapability after_reset = fixture.query("gpu-n", "cap.memory.capacity");
  HCR_CHECK(!after_reset.current);
  HCR_CHECK(after_reset.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK(after_reset.declared_support == SupportState::SupportedNative);
  HCR_CHECK(is_stale(after_reset.staleness));
  HCR_CHECK(after_reset.explanation.find("no current evidence") != std::string::npos);

  // Fresh evidence for the new generation becomes current again.
  HCR_REQUIRE(fixture.publish("gpu-n", "cap.memory.capacity", SupportState::SupportedNative,
                              CapabilityValue(BytesValue{4096}))
                  .accepted());
  const ResolvedCapability republished = fixture.query("gpu-n", "cap.memory.capacity");
  HCR_CHECK(republished.current);
  HCR_CHECK(republished.effective_support == EffectiveSupport::Supported);
}

HCR_TEST(reset, driver_firmware_and_runtime_changes_require_revalidation) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-o", "1.0", "1.0").accepted());
  HCR_REQUIRE(fixture.publish("gpu-o", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_CHECK(fixture.query("gpu-o", "cap.compute.fp32").usable());

  HCR_REQUIRE(fixture.add_device("gpu-o", "2.0", "1.0").accepted());
  const ResolvedCapability after_driver = fixture.query("gpu-o", "cap.compute.fp32");
  HCR_CHECK(after_driver.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK((after_driver.staleness & StalenessFlag::DriverGenerationChanged) != StalenessFlag::None);

  HCR_REQUIRE(fixture.publish("gpu-o", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_REQUIRE(fixture.add_device("gpu-o", "2.0", "2.0").accepted());
  const ResolvedCapability after_firmware = fixture.query("gpu-o", "cap.compute.fp32");
  HCR_CHECK((after_firmware.staleness & StalenessFlag::FirmwareGenerationChanged) != StalenessFlag::None);

  HCR_REQUIRE(fixture.publish("gpu-o", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  DeviceRegistration registration = fixture.device_registration("gpu-o", "2.0", "2.0");
  registration.software.runtime.version = "9.9";
  Publication publication;
  publication.kind = PublicationKind::RegisterDevice;
  publication.publisher = fixture.publisher;
  publication.sequence = fixture.next_sequence();
  publication.evidence_generation = EvidenceGeneration::first();
  publication.device = registration;
  HCR_REQUIRE(fixture.registry.apply(publication).accepted());
  const ResolvedCapability after_runtime = fixture.query("gpu-o", "cap.compute.fp32");
  HCR_CHECK((after_runtime.staleness & StalenessFlag::RuntimeGenerationChanged) != StalenessFlag::None);
}

HCR_TEST(reset, durable_curated_evidence_survives_a_device_reset) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-p").accepted());
  HCR_REQUIRE(fixture.publish("gpu-p", "cap.compute.bf16", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Curated, "curated.table",
                              SourceClass::StaticCuratedDatabase, ProvenanceClass::CuratedStatic)
                  .accepted());
  HCR_REQUIRE(fixture.publish("gpu-p", "cap.compute.bf16", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Probed, "live.probe",
                              SourceClass::HardwareProbe, ProvenanceClass::RealLiveHardware)
                  .accepted());
  HCR_REQUIRE(fixture.reset_device("gpu-p").accepted());

  const ResolvedCapability resolved = fixture.query("gpu-p", "cap.compute.bf16");
  HCR_CHECK(!resolved.current);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK_EQ(resolved.evidence.size(), std::size_t{2});
}

HCR_TEST(capability, withdrawal_removes_the_claim_without_asserting_non_support) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-q").accepted());
  HCR_REQUIRE(fixture.publish("gpu-q", "cap.net.roce", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_CHECK(fixture.query("gpu-q", "cap.net.roce").usable());

  HCR_REQUIRE(fixture.withdraw_capability("gpu-q", "cap.net.roce").accepted());
  const ResolvedCapability resolved = fixture.query("gpu-q", "cap.net.roce");
  HCR_CHECK(resolved.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK(resolved.declared_support != SupportState::Unsupported);
  HCR_CHECK((resolved.staleness & StalenessFlag::Withdrawn) != StalenessFlag::None);
}

HCR_TEST(conditional, support_is_never_flattened_into_unconditional) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-r").accepted());
  HCR_REQUIRE(fixture.publish("gpu-r", "cap.net.gpudirect", SupportState::SupportedConditional,
                              CapabilityValue(true), PrecisionClass::DirectReported, "driver.check",
                              SourceClass::DriverQuery, ProvenanceClass::RealRuntimeProbe,
                              min_driver("550.0"))
                  .accepted());

  EnvironmentContext missing;
  ResolvedCapability resolved = fixture.query("gpu-r", "cap.net.gpudirect", missing);
  HCR_CHECK(resolved.declared_support == SupportState::SupportedConditional);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::SupportedConditionalUnknown);

  EnvironmentContext satisfied;
  satisfied.set_driver_version("560.0");
  resolved = fixture.query("gpu-r", "cap.net.gpudirect", satisfied);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(resolved.condition_evaluation.outcome == ConditionOutcome::Satisfied);

  EnvironmentContext unmet;
  unmet.set_driver_version("500.0");
  resolved = fixture.query("gpu-r", "cap.net.gpudirect", unmet);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::SupportedConditionalUnmet);
  HCR_CHECK(!resolved.usable());
}

HCR_TEST(quirks, blocking_quirk_overrides_advertised_support_until_the_driver_changes) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-s", "535.0").accepted());
  HCR_REQUIRE(fixture.publish("gpu-s", "cap.interconnect.peer_to_peer", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_REQUIRE(fixture.publish_quirk("quirk.test.p2p", "cap.interconnect.peer_to_peer",
                                    QuirkSeverity::Unusable, QuirkGeneration::first(), "family.test",
                                    "530.0", "540.0")
                  .accepted());

  ResolvedCapability resolved = fixture.query("gpu-s", "cap.interconnect.peer_to_peer");
  HCR_CHECK(resolved.declared_support == SupportState::SupportedNative);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::BlockedByQuirk);
  HCR_REQUIRE(!resolved.quirks.empty());
  HCR_CHECK(resolved.quirks.front().result == QuirkApplicabilityResult::Applies);
  HCR_CHECK(resolved.quirks.front().severity == QuirkSeverity::Unusable);

  // A driver upgrade outside the affected range clears the block, and the
  // capability becomes usable again once it is republished for that generation.
  HCR_REQUIRE(fixture.add_device("gpu-s", "570.0").accepted());
  HCR_REQUIRE(fixture.publish("gpu-s", "cap.interconnect.peer_to_peer", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  resolved = fixture.query("gpu-s", "cap.interconnect.peer_to_peer");
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Supported);
  for (const QuirkApplication& application : resolved.quirks) {
    HCR_CHECK(application.result != QuirkApplicabilityResult::Applies);
  }
}

HCR_TEST(quirks, precedence_prefers_the_most_severe_applicable_quirk) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-t", "535.0").accepted());
  HCR_REQUIRE(fixture.publish("gpu-t", "cap.interconnect.peer_to_peer", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_REQUIRE(fixture.publish_quirk("quirk.test.caveat", "cap.interconnect.peer_to_peer",
                                    QuirkSeverity::Caveat, QuirkGeneration::first(), "family.test")
                  .accepted());
  HCR_REQUIRE(fixture.publish_quirk("quirk.test.blocker", "cap.interconnect.peer_to_peer",
                                    QuirkSeverity::Fatal, QuirkGeneration::first(), "family.test")
                  .accepted());
  const ResolvedCapability resolved = fixture.query("gpu-t", "cap.interconnect.peer_to_peer");
  HCR_CHECK(resolved.effective_support == EffectiveSupport::BlockedByQuirk);
  HCR_REQUIRE(resolved.quirks.size() >= 2u);
  HCR_CHECK(resolved.quirks.front().severity == QuirkSeverity::Fatal);
}

HCR_TEST(quirks, generation_rules_are_enforced) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-u").accepted());
  HCR_REQUIRE(fixture.publish_quirk("quirk.test.gen", "cap.compute.fp32", QuirkSeverity::Caveat,
                                    QuirkGeneration{2})
                  .accepted() == false);
  HCR_REQUIRE(fixture.publish_quirk("quirk.test.gen", "cap.compute.fp32", QuirkSeverity::Caveat,
                                    QuirkGeneration::first())
                  .accepted());
  HCR_CHECK_EQ(fixture.publish_quirk("quirk.test.gen", "cap.compute.fp32", QuirkSeverity::Caveat,
                                     QuirkGeneration::first())
                   .code,
               ApplyCode::AcceptedIdempotent);
  HCR_CHECK_EQ(fixture.publish_quirk("quirk.test.gen", "cap.compute.fp32", QuirkSeverity::Unusable,
                                     QuirkGeneration::first())
                   .code,
               ApplyCode::RejectedDuplicateConflict);
  HCR_CHECK(fixture.publish_quirk("quirk.test.gen", "cap.compute.fp32", QuirkSeverity::Unusable,
                                  QuirkGeneration{2})
                .accepted());
  HCR_REQUIRE(fixture.withdraw_quirk("quirk.test.gen", QuirkGeneration{2}).accepted());
  std::vector<QuirkRecord> quirks;
  HCR_CHECK_STATUS(fixture.registry.list_quirks(quirks));
  for (const QuirkRecord& quirk : quirks) {
    if (quirk.id == QuirkId::parse("quirk.test.gen")) HCR_CHECK(quirk.withdrawn);
  }
}

HCR_TEST(quirks, a_quirk_must_impact_a_known_capability) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-v").accepted());
  const ApplyResult result = fixture.publish_quirk("quirk.test.unknown", "cap.does.not.exist",
                                                   QuirkSeverity::Caveat);
  HCR_CHECK_EQ(result.code, ApplyCode::RejectedUnknownSchema);
}

HCR_TEST(compatibility, facts_require_resolvable_subjects) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-w").accepted());
  HardwareReference device_reference;
  device_reference.kind = HardwareReference::Kind::Device;
  device_reference.token = fixture.device_of("gpu-w").str();
  HardwareReference runtime_reference;
  runtime_reference.kind = HardwareReference::Kind::Runtime;
  runtime_reference.token = "runtime.test";
  runtime_reference.version = SoftwareVersion::parse("1.0");

  HCR_REQUIRE(fixture.publish_compatibility("compat.test.ok", device_reference, runtime_reference,
                                            CompatibilityOutcome::Compatible)
                  .accepted());

  HardwareReference dangling;
  dangling.kind = HardwareReference::Kind::Device;
  dangling.token = "dev.test.absent";
  HCR_CHECK_EQ(fixture
                   .publish_compatibility("compat.test.dangling", dangling, runtime_reference,
                                          CompatibilityOutcome::Compatible)
                   .code,
               ApplyCode::RejectedValidation);

  HardwareReference family_reference;
  family_reference.kind = HardwareReference::Kind::HardwareFamily;
  family_reference.token = "family.test";
  const ApplyResult generated = fixture.publish_compatibility("compat.test.gen", family_reference,
                                                              runtime_reference,
                                                              CompatibilityOutcome::Incompatible,
                                                              "incompatible by construction");
  HCR_REQUIRE(generated.accepted());
  HCR_CHECK_EQ(fixture
                   .publish_compatibility("compat.test.gen", family_reference, runtime_reference,
                                          CompatibilityOutcome::Incompatible, "changed reason")
                   .code,
               ApplyCode::RejectedDuplicateConflict);

  CompatibilityQuery query;
  query.reference = family_reference;
  std::vector<CompatibilityFact> facts;
  HCR_CHECK_STATUS(fixture.registry.query_compatibility(query, facts));
  HCR_CHECK_EQ(facts.size(), std::size_t{1});
}

HCR_TEST(compatibility, incompatible_facts_require_a_reason) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-x").accepted());
  HardwareReference family_reference;
  family_reference.kind = HardwareReference::Kind::HardwareFamily;
  family_reference.token = "family.test";
  HardwareReference runtime_reference;
  runtime_reference.kind = HardwareReference::Kind::Runtime;
  runtime_reference.token = "runtime.test";
  const ApplyResult result = fixture.publish_compatibility("compat.test.reason", family_reference,
                                                           runtime_reference,
                                                           CompatibilityOutcome::Incompatible, "");
  HCR_CHECK_EQ(result.code, ApplyCode::RejectedValidation);
}

HCR_TEST(comparison, reports_facts_without_ordering) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-y1").accepted());
  HCR_REQUIRE(fixture.add_device("gpu-y2").accepted());
  HCR_REQUIRE(fixture.publish("gpu-y1", "cap.memory.capacity", SupportState::SupportedNative,
                              CapabilityValue(BytesValue{1024}))
                  .accepted());
  HCR_REQUIRE(fixture.publish("gpu-y2", "cap.memory.capacity", SupportState::SupportedNative,
                              CapabilityValue(BytesValue{2048}))
                  .accepted());
  HCR_REQUIRE(fixture.publish("gpu-y1", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());

  ComparisonRequest request;
  request.left = fixture.device_of("gpu-y1");
  request.right = fixture.device_of("gpu-y2");
  DeviceComparison comparison;
  HCR_CHECK_STATUS(fixture.registry.compare_devices(request, comparison));
  bool saw_value_difference = false;
  bool saw_only_left = false;
  for (const CapabilityDifference& difference : comparison.differences) {
    if (difference.capability == CapabilityId::parse("cap.memory.capacity") &&
        difference.kind == CapabilityDifferenceKind::ValueDiffers) {
      saw_value_difference = true;
    }
    if (difference.capability == CapabilityId::parse("cap.compute.fp32") &&
        difference.kind == CapabilityDifferenceKind::OnlyInLeft) {
      saw_only_left = true;
    }
  }
  HCR_CHECK(saw_value_difference);
  HCR_CHECK(saw_only_left);
  HCR_CHECK(comparison.explanation.find("never an ordering") != std::string::npos);

  DeviceComparison repeat;
  HCR_CHECK_STATUS(fixture.registry.compare_devices(request, repeat));
  HCR_CHECK_EQ(render_comparison(comparison), render_comparison(repeat));
}

HCR_TEST(snapshot, snapshots_are_immutable_and_staleness_is_detectable) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-z").accepted());
  HCR_REQUIRE(fixture.publish("gpu-z", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  RegistrySnapshot first;
  HCR_CHECK_STATUS(fixture.registry.snapshot(first));
  HCR_CHECK(first.subjects.size() == 1u);
  HCR_CHECK(first.capabilities.size() == 1u);
  HCR_CHECK_EQ(first.real_statements, std::size_t{1});
  HCR_CHECK(first.canonical_digest != 0u);

  RegistrySnapshot repeat;
  HCR_CHECK_STATUS(fixture.registry.snapshot(repeat));
  HCR_CHECK(repeat.generation > first.generation);
  HCR_CHECK(snapshot_is_current(first, repeat) == false);

  HCR_REQUIRE(fixture.publish("gpu-z", "cap.compute.fp16", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  RegistrySnapshot third;
  HCR_CHECK_STATUS(fixture.registry.snapshot(third));
  const StalenessFlag flags = detect_snapshot_staleness(first, third);
  HCR_CHECK(is_stale(flags));
  HCR_CHECK((flags & StalenessFlag::SupersededByNewerEvidence) != StalenessFlag::None);
}

HCR_TEST(determinism, identical_state_yields_identical_answers) {
  const auto build = []() {
    Fixture fixture;
    HCR_REQUIRE(fixture.add_device("gpu-det-1").accepted());
    HCR_REQUIRE(fixture.add_device("gpu-det-2").accepted());
    HCR_REQUIRE(fixture.publish("gpu-det-1", "cap.compute.fp32", SupportState::SupportedNative,
                                CapabilityValue(true))
                    .accepted());
    HCR_REQUIRE(fixture.publish("gpu-det-2", "cap.compute.fp32", SupportState::Unsupported,
                                CapabilityValue{}, PrecisionClass::Exact)
                    .accepted());
    DeviceCapabilityQuery query;
    query.device = fixture.device_of("gpu-det-1");
    query.only_current = false;
    std::vector<ResolvedCapability> capabilities;
    HCR_CHECK_STATUS(fixture.registry.query_capabilities(query, capabilities));
    std::string rendered;
    for (const ResolvedCapability& capability : capabilities) {
      rendered += render_capability_brief(capability);
      rendered += "\n";
      rendered += capability.explanation;
      rendered += "\n";
    }
    std::vector<SubjectSnapshot> subjects;
    HCR_CHECK_STATUS(fixture.registry.list_devices(subjects));
    for (const SubjectSnapshot& subject : subjects) rendered += render_subject(subject);
    RegistrySnapshot snapshot;
    HCR_CHECK_STATUS(fixture.registry.snapshot(snapshot));
    rendered += hex64(canonical_snapshot_digest(snapshot));
    return rendered;
  };
  const std::string first = build();
  const std::string second = build();
  HCR_CHECK_EQ(first, second);
}

HCR_TEST(limits, device_limit_is_enforced) {
  RegistryLimits limits;
  limits.max_devices = 2;
  CapabilityRegistry registry(limits);
  PublisherIdentity identity;
  identity.id = PublisherId::parse("publisher.limits");
  const ApplyResult publisher = registry.register_publisher(identity, "test");
  HCR_REQUIRE(publisher.accepted());
  std::uint64_t sequence = 1;
  const auto register_device = [&](const char* token) {
    DeviceRegistration registration;
    registration.identity.id = DeviceId::parse(token);
    registration.identity.incarnation = DeviceIncarnationId::parse(std::string(token) + ".inc");
    registration.identity.hardware.vendor = VendorId::parse("vendor.test");
    registration.identity.hardware.family = HardwareFamilyId::parse("family.test");
    registration.identity.hardware.model = HardwareModelId::parse("model.test");
    registration.identity.hardware.architecture = ArchitectureId::parse("arch.test");
    registration.identity.hardware.hardware_class = HardwareClass::Gpu;
    registration.identity.platform = PlatformId::parse("platform.test");
    registration.identity.display_name = token;
    registration.platform.id = PlatformId::parse("platform.test");
    Publication publication;
    publication.kind = PublicationKind::RegisterDevice;
    publication.publisher = publisher.assigned_publisher;
    publication.sequence = sequence++;
    publication.evidence_generation = EvidenceGeneration::first();
    publication.device = registration;
    return registry.apply(publication);
  };
  HCR_CHECK(register_device("dev.one").accepted());
  HCR_CHECK(register_device("dev.two").accepted());
  HCR_CHECK_EQ(register_device("dev.three").code, ApplyCode::RejectedLimit);
  HCR_CHECK_EQ(registry.resource_usage().devices, std::size_t{2});
}

HCR_TEST(limits, evidence_history_is_bounded_and_evicts_deterministically) {
  RegistryLimits limits;
  limits.max_evidence_history = 4;
  limits.max_evidence_per_capability = 1;
  CapabilityRegistry registry(limits);
  PublisherIdentity identity;
  identity.id = PublisherId::parse("publisher.history");
  const ApplyResult publisher = registry.register_publisher(identity, "test");
  HCR_REQUIRE(publisher.accepted());

  DeviceRegistration registration;
  registration.identity.id = DeviceId::parse("dev.history");
  registration.identity.incarnation = DeviceIncarnationId::parse("dev.history.inc");
  registration.identity.hardware.vendor = VendorId::parse("vendor.test");
  registration.identity.hardware.family = HardwareFamilyId::parse("family.test");
  registration.identity.hardware.model = HardwareModelId::parse("model.test");
  registration.identity.hardware.architecture = ArchitectureId::parse("arch.test");
  registration.identity.hardware.hardware_class = HardwareClass::Gpu;
  registration.identity.platform = PlatformId::parse("platform.test");
  registration.identity.display_name = "history";
  registration.platform.id = PlatformId::parse("platform.test");
  Publication registration_publication;
  registration_publication.kind = PublicationKind::RegisterDevice;
  registration_publication.publisher = publisher.assigned_publisher;
  registration_publication.sequence = 1;
  registration_publication.evidence_generation = EvidenceGeneration::first();
  registration_publication.device = registration;
  HCR_REQUIRE(registry.apply(registration_publication).accepted());

  for (int i = 0; i < 40; ++i) {
    Publication publication;
    publication.kind = PublicationKind::PublishCapability;
    publication.publisher = publisher.assigned_publisher;
    publication.sequence = static_cast<std::uint64_t>(2 + i);
    publication.evidence_generation = EvidenceGeneration::first();
    publication.capability.device = registration.identity.id;
    publication.capability.device_generation = DeviceGeneration::first();
    publication.capability.device_boot = DeviceBootId::first();
    publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
    publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v1");
    publication.capability.support = (i % 2 == 0) ? SupportState::SupportedNative : SupportState::Disabled;
    publication.capability.value = CapabilityValue(true);
    publication.capability.precision = PrecisionClass::DirectReported;
    publication.capability.source.source_class = SourceClass::RuntimeProbe;
    publication.capability.source.provenance = ProvenanceClass::RealRuntimeProbe;
    publication.capability.source.adapter = "test";
    publication.capability.source.native_reference = "history";
    HCR_CHECK(registry.apply(publication).accepted());
  }
  HCR_CHECK(registry.resource_usage().evidence_records <= limits.max_evidence_history);
  HCR_CHECK(registry.resource_usage().evidence_records > 0u);
}

HCR_TEST(lifecycle, shutdown_rejects_further_publication) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-shutdown").accepted());
  HCR_CHECK_STATUS(fixture.registry.begin_shutdown());
  HCR_CHECK(fixture.registry.shutting_down());
  const ApplyResult rejected = fixture.publish("gpu-shutdown", "cap.compute.fp32",
                                               SupportState::SupportedNative, CapabilityValue(true));
  HCR_CHECK_EQ(rejected.code, ApplyCode::RejectedShuttingDown);
  HCR_CHECK(rejected.detail.find("no longer accepts evidence") != std::string::npos);
  HCR_CHECK_STATUS(fixture.registry.clear_shutdown());
  HCR_CHECK(fixture.publish("gpu-shutdown", "cap.compute.fp32", SupportState::SupportedNative,
                            CapabilityValue(true))
                .accepted());
}

HCR_TEST(lifecycle, repeated_begin_and_end_leaves_consistent_state) {
  Fixture fixture;
  for (int round = 0; round < 3; ++round) {
    HCR_REQUIRE(fixture.add_device("gpu-life").accepted());
    HCR_CHECK_STATUS(fixture.registry.begin_shutdown());
    HCR_CHECK_EQ(fixture.publish("gpu-life", "cap.compute.fp32", SupportState::SupportedNative,
                                 CapabilityValue(true))
                     .code,
                 ApplyCode::RejectedShuttingDown);
    HCR_CHECK_STATUS(fixture.registry.clear_shutdown());
    HCR_CHECK(fixture.publish("gpu-life", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
    HCR_CHECK(fixture.query("gpu-life", "cap.compute.fp32").usable());
  }
}

HCR_TEST(retirement, retiring_a_boot_removes_its_authority) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("gpu-retire").accepted());
  HCR_REQUIRE(fixture.publish("gpu-retire", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_CHECK(fixture.query("gpu-retire", "cap.compute.fp32").usable());
  HCR_REQUIRE(fixture.registry
                  .retire_publisher_boot(fixture.publisher.id, fixture.publisher.boot, "process died")
                  .accepted());
  const ResolvedCapability resolved = fixture.query("gpu-retire", "cap.compute.fp32");
  HCR_CHECK(resolved.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK((resolved.staleness & StalenessFlag::PublisherFenced) != StalenessFlag::None);
}
