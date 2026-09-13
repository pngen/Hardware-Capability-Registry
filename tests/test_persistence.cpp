// Hardware Capability Registry - persistence and recovery tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/detail/binary.hpp"
#include "hcr/persistence.hpp"
#include "hcr/protocol.hpp"

using namespace hcr;
using namespace hcrtest;

namespace {

std::string temporary_path(const char* name) {
  return std::string("hcr-test-") + name + ".state";
}

struct ScopedFile {
  explicit ScopedFile(std::string file_path) : path(std::move(file_path)) {}
  ~ScopedFile() { std::remove(path.c_str()); }
  std::string path;
};

void write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  HCR_REQUIRE(file != nullptr);
  if (!bytes.empty()) {
    HCR_REQUIRE(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
  }
  std::fclose(file);
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  HCR_REQUIRE(file != nullptr);
  std::vector<std::uint8_t> bytes;
  std::uint8_t buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    bytes.insert(bytes.end(), buffer, buffer + read);
  }
  std::fclose(file);
  return bytes;
}

/// Wraps an arbitrary payload in a valid container prefix so that payload-level
/// validation can be exercised independently of container integrity.
std::vector<std::uint8_t> wrap_payload(const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> image;
  detail::ByteWriter writer(image);
  writer.u32(0x53524348u);
  writer.u16(static_cast<std::uint16_t>(state_format_version()));
  writer.u16(0u);
  writer.u32(static_cast<std::uint32_t>(payload.size()));
  writer.u32(crc32(payload.data(), payload.size()));
  writer.u64(0u);
  image.insert(image.end(), payload.begin(), payload.end());
  return image;
}

/// Builds a state with one device, one live capability and one curated fact.
DurableState build_state(Fixture& fixture) {
  HCR_REQUIRE(fixture.add_device("persist-gpu").accepted());
  HCR_REQUIRE(fixture.publish("persist-gpu", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Probed, "live.probe",
                              SourceClass::HardwareProbe, ProvenanceClass::RealLiveHardware)
                  .accepted());
  HCR_REQUIRE(fixture.publish("persist-gpu", "cap.compute.bf16", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Curated, "curated.table",
                              SourceClass::StaticCuratedDatabase, ProvenanceClass::CuratedStatic)
                  .accepted());
  HCR_REQUIRE(fixture.publish_quirk("quirk.persist", "cap.compute.fp32", QuirkSeverity::Caveat,
                                    QuirkGeneration::first(), "family.test")
                  .accepted());
  HardwareReference family_reference;
  family_reference.kind = HardwareReference::Kind::HardwareFamily;
  family_reference.token = "family.test";
  HardwareReference runtime_reference;
  runtime_reference.kind = HardwareReference::Kind::Runtime;
  runtime_reference.token = "runtime.test";
  HCR_REQUIRE(fixture
                  .publish_compatibility("compat.persist", family_reference, runtime_reference,
                                         CompatibilityOutcome::CompatibleConditional, "curated")
                  .accepted());
  return fixture.registry.export_durable_state();
}

}  // namespace

HCR_TEST(persistence, durable_state_round_trips) {
  Fixture fixture;
  const DurableState state = build_state(fixture);
  PersistOptions options;
  std::vector<std::uint8_t> bytes;
  HCR_CHECK_STATUS(encode_durable_state(state, options, bytes));
  HCR_CHECK(!bytes.empty());

  DurableState decoded;
  HCR_CHECK_STATUS(decode_durable_state(bytes, options, decoded));
  HCR_CHECK_EQ(decoded.subjects.size(), state.subjects.size());
  HCR_CHECK_EQ(decoded.capability_history.size(), state.capability_history.size());
  HCR_CHECK_EQ(decoded.quirks.size(), state.quirks.size());
  HCR_CHECK_EQ(decoded.compatibility.size(), state.compatibility.size());
  HCR_CHECK_EQ(decoded.canonical_digest, state.canonical_digest);
}

HCR_TEST(persistence, reimport_restores_curated_truth_and_requires_revalidation_for_live_truth) {
  Fixture fixture;
  std::vector<std::uint8_t> bytes;
  PersistOptions options;
  HCR_CHECK_STATUS(encode_durable_state(build_state(fixture), options, bytes));

  DurableState decoded;
  HCR_CHECK_STATUS(decode_durable_state(bytes, options, decoded));

  CapabilityRegistry restored;
  ImportReport report;
  HCR_CHECK_STATUS(restored.import_durable_state(decoded, report));
  HCR_CHECK(report.applied);
  HCR_CHECK(report.revalidated_records >= 1u);

  CapabilityQuery query;
  query.device = canonical_device_id("dev.test", "persist-gpu");
  query.capability = CapabilityId::parse("cap.compute.fp32");
  ResolvedCapability live;
  HCR_CHECK_STATUS(restored.query_capability(query, live));
  HCR_CHECK(live.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK((live.staleness & StalenessFlag::PublisherFenced) != StalenessFlag::None);

  // Curated, durable knowledge is preserved and stays current.
  query.capability = CapabilityId::parse("cap.compute.bf16");
  ResolvedCapability curated;
  HCR_CHECK_STATUS(restored.query_capability(query, curated));
  HCR_CHECK(curated.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(curated.precision == PrecisionClass::Curated);
  HCR_CHECK(curated.truth == TruthClass::Real);

  // Publisher liveness is never restored.
  std::vector<PublisherRecord> publishers;
  HCR_CHECK_STATUS(restored.list_publishers(PublisherQuery{}, publishers));
  HCR_REQUIRE(!publishers.empty());
  for (const PublisherRecord& publisher : publishers) {
    HCR_CHECK(publisher.state != PublisherState::Registered);
  }
}

HCR_TEST(persistence, replay_watermarks_survive_a_restart) {
  Fixture fixture;
  const DurableState state = build_state(fixture);
  CapabilityRegistry restored;
  ImportReport report;
  HCR_CHECK_STATUS(restored.import_durable_state(state, report));

  // A frame from the pre-restart boot identity must be rejected even though the
  // durable watermark was preserved.
  Publication replay;
  replay.kind = PublicationKind::PublishCapability;
  replay.publisher = fixture.publisher;
  replay.sequence = 1;
  replay.evidence_generation = EvidenceGeneration::first();
  replay.capability.device = canonical_device_id("dev.test", "persist-gpu");
  replay.capability.device_generation = DeviceGeneration::first();
  replay.capability.device_boot = DeviceBootId::first();
  replay.capability.capability = CapabilityId::parse("cap.compute.fp32");
  replay.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v1");
  replay.capability.support = SupportState::SupportedNative;
  replay.capability.value = CapabilityValue(true);
  replay.capability.source.source_class = SourceClass::HardwareProbe;
  replay.capability.source.provenance = ProvenanceClass::RealLiveHardware;
  replay.capability.source.adapter = "test";
  const ApplyResult rejected = restored.apply(replay);
  HCR_CHECK(rejected.code == ApplyCode::RejectedFencedPublisher ||
            rejected.code == ApplyCode::RejectedStaleBoot);
}

HCR_TEST(persistence, container_corruption_is_rejected) {
  Fixture fixture;
  PersistOptions options;
  std::vector<std::uint8_t> valid;
  HCR_CHECK_STATUS(encode_durable_state(build_state(fixture), options, valid));

  DurableState decoded;
  HCR_CHECK(!decode_durable_state({}, options, decoded).ok());

  std::vector<std::uint8_t> truncated(valid.begin(), valid.begin() + 10);
  HCR_CHECK(!decode_durable_state(truncated, options, decoded).ok());

  std::vector<std::uint8_t> bad_magic = valid;
  bad_magic[0] = static_cast<std::uint8_t>(bad_magic[0] ^ 0xFFu);
  HCR_CHECK_EQ(decode_durable_state(bad_magic, options, decoded).code(), ErrorCode::CorruptData);

  std::vector<std::uint8_t> bad_version = valid;
  bad_version[4] = 9;
  HCR_CHECK_EQ(decode_durable_state(bad_version, options, decoded).code(), ErrorCode::UnsupportedVersion);

  std::vector<std::uint8_t> bad_flags = valid;
  bad_flags[6] = 1;
  HCR_CHECK_EQ(decode_durable_state(bad_flags, options, decoded).code(), ErrorCode::CorruptData);

  std::vector<std::uint8_t> bad_length = valid;
  bad_length[8] = static_cast<std::uint8_t>(bad_length[8] + 7u);
  HCR_CHECK_EQ(decode_durable_state(bad_length, options, decoded).code(), ErrorCode::CorruptData);

  std::vector<std::uint8_t> bad_checksum = valid;
  bad_checksum.back() = static_cast<std::uint8_t>(bad_checksum.back() ^ 0x5Au);
  HCR_CHECK_EQ(decode_durable_state(bad_checksum, options, decoded).code(), ErrorCode::IntegrityFailure);

  std::vector<std::uint8_t> trailing = valid;
  trailing.push_back(0u);
  HCR_CHECK(!decode_durable_state(trailing, options, decoded).ok());
}

HCR_TEST(persistence, payload_level_corruption_is_rejected) {
  PersistOptions options;

  // Absurd section count.
  {
    std::vector<std::uint8_t> payload;
    detail::ByteWriter writer(payload);
    writer.u32(state_format_version());
    writer.u64(1);
    writer.u64(1);
    writer.u64(1);
    writer.str("", 128);
    writer.str("test", 128);
    writer.u32(0xFFFFFFFFu);
    DurableState decoded;
    HCR_CHECK_EQ(decode_durable_state(wrap_payload(payload), options, decoded).code(), ErrorCode::LimitExceeded);
  }
  // Header format-version mismatch inside the payload.
  {
    std::vector<std::uint8_t> payload;
    detail::ByteWriter writer(payload);
    writer.u32(99);
    writer.u64(1);
    writer.u64(1);
    writer.u64(1);
    writer.str("", 128);
    writer.str("test", 128);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    writer.u64(0);
    DurableState decoded;
    HCR_CHECK_EQ(decode_durable_state(wrap_payload(payload), options, decoded).code(),
                 ErrorCode::UnsupportedVersion);
  }
}

HCR_TEST(persistence, invalid_state_is_never_partially_applied) {
  Fixture fixture;
  DurableState state = build_state(fixture);

  // 1. duplicate device identity
  {
    DurableState mutated = state;
    mutated.subjects.push_back(mutated.subjects.front());
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidIdentity);
    HCR_CHECK(!report.applied);
    HCR_CHECK_EQ(target.resource_usage().devices, std::size_t{0});
  }
  // 2. dangling capability reference
  {
    DurableState mutated = state;
    mutated.capability_history.front().device = DeviceId::parse("dev.test.absent");
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidReference);
    HCR_CHECK_EQ(target.resource_usage().evidence_records, std::size_t{0});
  }
  // 3. capability generation regression
  {
    DurableState mutated = state;
    for (CapabilityRecord& record : mutated.capability_history) {
      if (record.capability == CapabilityId::parse("cap.compute.fp32")) {
        record.capability_generation = CapabilityGeneration{};
      }
    }
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    const Status status = target.import_durable_state(mutated, report);
    HCR_CHECK(!status.ok() || report.applied);
    HCR_CHECK_EQ(target.resource_usage().devices, status.ok() ? std::size_t{1} : std::size_t{0});
  }
  // 4. invalid support state is caught by record validation
  {
    DurableState mutated = state;
    mutated.capability_history.front().schema = CapabilitySchemaId::parse("cap.compute.fp16.v1");
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidCapability);
  }
  // 5. broken quirk reference
  {
    DurableState mutated = state;
    mutated.quirks.front().impacted_capabilities = {CapabilityId::parse("cap.absent.capability")};
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidReference);
  }
  // 6. broken compatibility reference
  {
    DurableState mutated = state;
    mutated.compatibility.front().lhs.token = "family.absent";
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidReference);
  }
  // 7. contradictory current generations
  {
    DurableState mutated = state;
    mutated.subjects.front().generation = DeviceGeneration{};
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK(!target.import_durable_state(mutated, report).ok());
  }
  // 8. corrupt digest
  {
    DurableState mutated = state;
    mutated.canonical_digest = mutated.canonical_digest ^ 0x1u;
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::IntegrityFailure);
    HCR_CHECK_EQ(target.resource_usage().devices, std::size_t{0});
  }
  // 9. invalid provenance (stronger than the source class allows)
  {
    DurableState mutated = state;
    mutated.capability_history.front().source.source_class = SourceClass::StaticCuratedDatabase;
    mutated.capability_history.front().source.provenance = ProvenanceClass::RealLiveHardware;
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::InvalidProvenance);
  }
  // 10. unsupported format version
  {
    DurableState mutated = state;
    mutated.header.format_version = 99;
    mutated.canonical_digest = canonical_durable_digest(mutated);
    CapabilityRegistry target;
    ImportReport report;
    HCR_CHECK_EQ(target.import_durable_state(mutated, report).code(), ErrorCode::UnsupportedVersion);
  }
}

HCR_TEST(persistence, file_store_writes_atomically_and_reloads) {
  Fixture fixture;
  ScopedFile file(temporary_path("roundtrip"));
  PersistOptions options;
  FileStateStore store(file.path, options);
  const DurableState state = build_state(fixture);
  HCR_CHECK_STATUS(store.save(state));

  DurableState loaded;
  HCR_CHECK_STATUS(store.load(loaded));
  HCR_CHECK_EQ(loaded.canonical_digest, state.canonical_digest);

  // A rejected save must leave the previous contents intact.
  PersistOptions tight;
  tight.max_bytes = 64;
  FileStateStore tight_store(file.path, tight);
  HCR_CHECK(!tight_store.save(state).ok());
  DurableState still_valid;
  HCR_CHECK_STATUS(store.load(still_valid));
  HCR_CHECK_EQ(still_valid.canonical_digest, state.canonical_digest);

  // A partially written file is rejected and nothing is applied.
  std::vector<std::uint8_t> bytes = read_bytes(file.path);
  bytes.resize(bytes.size() / 2);
  write_bytes(file.path, bytes);
  DurableState partial;
  HCR_CHECK(!store.load(partial).ok());
}

HCR_TEST(persistence, an_older_image_never_resurrects_newer_truth) {
  Fixture fixture;
  PersistOptions options;
  std::vector<std::uint8_t> bytes;
  HCR_CHECK_STATUS(encode_durable_state(build_state(fixture), options, bytes));

  // New evidence arrives after the image was taken.
  HCR_REQUIRE(fixture.publish("persist-gpu", "cap.compute.fp8", SupportState::SupportedNative,
                              CapabilityValue(true), PrecisionClass::Probed, "later.probe",
                              SourceClass::HardwareProbe, ProvenanceClass::RealLiveHardware)
                  .accepted());

  CapabilityRegistry restored;
  ImportReport report;
  DurableState decoded;
  HCR_CHECK_STATUS(decode_durable_state(bytes, options, decoded));
  HCR_CHECK_STATUS(restored.import_durable_state(decoded, report));

  CapabilityQuery query;
  query.device = canonical_device_id("dev.test", "persist-gpu");
  query.capability = CapabilityId::parse("cap.compute.fp8");
  ResolvedCapability resolved;
  HCR_CHECK_STATUS(restored.query_capability(query, resolved));
  HCR_CHECK(!resolved.known);
  HCR_CHECK(resolved.effective_support == EffectiveSupport::Unknown);
}
