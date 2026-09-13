// Hardware Capability Registry - framing and codec adversarial tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/detail/binary.hpp"
#include "hcr/protocol.hpp"

using namespace hcr;
using namespace hcrtest;

namespace {

std::vector<Frame> decode_bytes(const std::vector<std::uint8_t>& bytes, RegistryLimits limits,
                                Status& status) {
  FrameDecoder decoder(std::move(limits));
  std::vector<Frame> frames;
  status = decoder.feed(bytes.data(), bytes.size(), frames);
  return frames;
}

Publication sample_publication(const Fixture& fixture) {
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher = fixture.publisher;
  publication.sequence = 7;
  publication.evidence_generation = EvidenceGeneration{3};
  publication.capability.device = canonical_device_id("dev.test", "proto");
  publication.capability.device_generation = DeviceGeneration{2};
  publication.capability.device_boot = DeviceBootId{2};
  publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v1");
  publication.capability.capability_generation = CapabilityGeneration{1};
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.precision = PrecisionClass::Probed;
  publication.capability.source.source_class = SourceClass::HardwareProbe;
  publication.capability.source.provenance = ProvenanceClass::RealLiveHardware;
  publication.capability.source.adapter = "test";
  publication.capability.source.native_reference = "probe";
  publication.capability.source.observed_at_utc = "2026-01-01T00:00:00Z";
  publication.capability.source.evidence_generation = EvidenceGeneration{3};
  publication.capability.conditions =
      std::make_shared<const ConditionNode>(ConditionKind::MinDriverVersion, "", "550.0");
  publication.reason = "test";
  publication.issued_at_utc = "2026-01-01T00:00:00Z";
  return publication;
}

}  // namespace

HCR_TEST(framing, frames_round_trip_and_are_bounded) {
  RegistryLimits limits;
  Frame frame;
  frame.type = MessageType::QueryRequest;
  frame.flags = kFrameFlagControl;
  frame.correlation = 42;
  frame.payload = {1, 2, 3, 4, 5};
  std::vector<std::uint8_t> bytes;
  HCR_CHECK_STATUS(encode_frame(frame, limits, bytes));
  HCR_CHECK_EQ(bytes.size(), kFrameHeaderSize + 5);

  Status status;
  auto frames = decode_bytes(bytes, limits, status);
  HCR_CHECK_STATUS(status);
  HCR_REQUIRE(frames.size() == 1u);
  HCR_CHECK(frames.front().type == MessageType::QueryRequest);
  HCR_CHECK_EQ(frames.front().correlation, std::uint64_t{42});
  HCR_CHECK(frames.front().payload == frame.payload);

  // Incremental decoding across arbitrary split points produces one frame.
  FrameDecoder decoder(limits);
  std::vector<Frame> collected;
  for (const std::uint8_t byte : bytes) {
    HCR_CHECK_STATUS(decoder.feed(&byte, 1, collected));
  }
  HCR_CHECK_EQ(collected.size(), std::size_t{1});
  HCR_CHECK_STATUS(decoder.finish());
}

HCR_TEST(framing, adversarial_frames_are_rejected_before_interpretation) {
  RegistryLimits limits;
  Frame frame;
  frame.type = MessageType::Heartbeat;
  frame.payload = {9, 9, 9};
  std::vector<std::uint8_t> valid;
  HCR_CHECK_STATUS(encode_frame(frame, limits, valid));

  Status status;
  HCR_CHECK(decode_bytes({}, limits, status).empty());
  HCR_CHECK_STATUS(status);

  std::vector<std::uint8_t> truncated(valid.begin(), valid.begin() + 12);
  HCR_CHECK(decode_bytes(truncated, limits, status).empty());
  HCR_CHECK_STATUS(status);
  FrameDecoder partial(limits);
  std::vector<Frame> frames;
  HCR_CHECK_STATUS(partial.feed(truncated.data(), truncated.size(), frames));
  HCR_CHECK(!partial.finish().ok());

  std::vector<std::uint8_t> wrong_magic = valid;
  wrong_magic[0] = 0x00;
  HCR_CHECK(!decode_bytes(wrong_magic, limits, status).empty() == false);
  HCR_CHECK_EQ(status.code(), ErrorCode::ProtocolError);

  std::vector<std::uint8_t> wrong_version = valid;
  wrong_version[4] = 9;
  decode_bytes(wrong_version, limits, status);
  HCR_CHECK_EQ(status.code(), ErrorCode::UnsupportedVersion);

  std::vector<std::uint8_t> unknown_type = valid;
  unknown_type[6] = 0xEE;
  unknown_type[7] = 0xEE;
  decode_bytes(unknown_type, limits, status);
  HCR_CHECK_EQ(status.code(), ErrorCode::ProtocolError);

  std::vector<std::uint8_t> invalid_flags = valid;
  invalid_flags[8] = 0x80;
  decode_bytes(invalid_flags, limits, status);
  HCR_CHECK_EQ(status.code(), ErrorCode::ProtocolError);

  std::vector<std::uint8_t> oversized = valid;
  oversized[12] = 0xFF;
  oversized[13] = 0xFF;
  oversized[14] = 0xFF;
  oversized[15] = 0x7F;
  decode_bytes(oversized, limits, status);
  HCR_CHECK_EQ(status.code(), ErrorCode::LimitExceeded);

  std::vector<std::uint8_t> corrupt_checksum = valid;
  corrupt_checksum[kFrameHeaderSize] = 0x00;
  decode_bytes(corrupt_checksum, limits, status);
  HCR_CHECK_EQ(status.code(), ErrorCode::IntegrityFailure);

  // The decoder refuses to buffer more than one maximal frame.
  RegistryLimits tiny;
  tiny.max_frame_bytes = 8;
  Frame big;
  big.type = MessageType::Publication;
  big.payload.assign(64, 0u);
  std::vector<std::uint8_t> encoded;
  HCR_CHECK(!encode_frame(big, tiny, encoded).ok());
}

HCR_TEST(codec, publication_round_trips_losslessly) {
  Fixture fixture;
  const Publication publication = sample_publication(fixture);
  RegistryLimits limits;
  std::vector<std::uint8_t> bytes;
  HCR_CHECK_STATUS(encode_publication(publication, limits, bytes));

  Publication decoded;
  HCR_CHECK_STATUS(decode_publication(bytes.data(), bytes.size(), limits, decoded));
  HCR_CHECK(decoded.kind == publication.kind);
  HCR_CHECK(decoded.publisher == publication.publisher);
  HCR_CHECK_EQ(decoded.sequence, publication.sequence);
  HCR_CHECK_EQ(decoded.evidence_generation.value(), publication.evidence_generation.value());
  HCR_CHECK(decoded.capability.device == publication.capability.device);
  HCR_CHECK(decoded.capability.capability == publication.capability.capability);
  HCR_CHECK(decoded.capability.schema == publication.capability.schema);
  HCR_CHECK(decoded.capability.support == publication.capability.support);
  HCR_CHECK(decoded.capability.value == publication.capability.value);
  HCR_CHECK(decoded.capability.precision == publication.capability.precision);
  HCR_CHECK(decoded.capability.source.source_class == publication.capability.source.source_class);
  HCR_CHECK(decoded.capability.source.provenance == publication.capability.source.provenance);
  HCR_CHECK_EQ(describe_condition(decoded.capability.conditions.get()),
               describe_condition(publication.capability.conditions.get()));
}

HCR_TEST(codec, malformed_payloads_are_rejected) {
  RegistryLimits limits;
  Publication decoded;

  // Unknown publication kind.
  {
    std::vector<std::uint8_t> bytes;
    detail::ByteWriter writer(bytes);
    writer.u16(0xFFFFu);
    HCR_CHECK(!decode_publication(bytes.data(), bytes.size(), limits, decoded).ok());
  }
  // Unknown support state.
  {
    Publication publication;
    publication.kind = PublicationKind::PublishCapability;
    publication.publisher.id = PublisherId::parse("publisher.test");
    publication.publisher.boot = PublisherBootId{1};
    publication.publisher.epoch = CoordinatorEpoch{1};
    std::vector<std::uint8_t> bytes;
    HCR_CHECK_STATUS(encode_publication(publication, limits, bytes));
    // Locate the support-state field and set it out of range: the support state
    // is written after device, capability and schema tokens, so a full patch is
    // simpler to express by re-encoding with a mutated enum.
    Publication mutated = publication;
    mutated.capability.support = static_cast<SupportState>(99);
    std::vector<std::uint8_t> mutated_bytes;
    HCR_CHECK_STATUS(encode_publication(mutated, limits, mutated_bytes));
    HCR_CHECK(!decode_publication(mutated_bytes.data(), mutated_bytes.size(), limits, decoded).ok());
  }
  // Trailing bytes.
  {
    Fixture fixture;
    std::vector<std::uint8_t> bytes;
    const Publication publication = sample_publication(fixture);
    HCR_CHECK_STATUS(encode_publication(publication, limits, bytes));
    bytes.push_back(0u);
    HCR_CHECK_EQ(decode_publication(bytes.data(), bytes.size(), limits, decoded).code(),
                 ErrorCode::ProtocolError);
  }
  // Truncated payload.
  {
    Fixture fixture;
    std::vector<std::uint8_t> bytes;
    HCR_CHECK_STATUS(encode_publication(sample_publication(fixture), limits, bytes));
    bytes.resize(bytes.size() / 2);
    HCR_CHECK(!decode_publication(bytes.data(), bytes.size(), limits, decoded).ok());
  }
}

HCR_TEST(codec, oversized_strings_are_bounded) {
  RegistryLimits limits;
  limits.max_metadata_bytes = 8;
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.publisher.id = PublisherId::parse("publisher.test");
  publication.publisher.boot = PublisherBootId{1};
  publication.publisher.epoch = CoordinatorEpoch{1};
  publication.capability.device = DeviceId::parse("dev.test");
  publication.capability.capability = CapabilityId::parse("cap.compute.fp32");
  publication.capability.schema = CapabilitySchemaId::parse("cap.compute.fp32.v1");
  publication.capability.support = SupportState::SupportedNative;
  publication.capability.value = CapabilityValue(true);
  publication.capability.source.source_class = SourceClass::HardwareProbe;
  publication.capability.source.provenance = ProvenanceClass::RealLiveHardware;
  publication.capability.source.adapter = std::string(64, 'a');
  // The encoder refuses to emit a field the receiver would reject: bounds are
  // never enforced by silently truncating data.
  std::vector<std::uint8_t> bytes;
  HCR_CHECK(!encode_publication(publication, limits, bytes).ok());
}

HCR_TEST(codec, messages_round_trip) {
  RegistryLimits limits;
  std::vector<std::uint8_t> payload;

  HelloMessage hello;
  hello.protocol_version = protocol_version();
  hello.agent = "hcr-test";
  hello.role = "client";
  hello.nonce = 17;
  HCR_CHECK_STATUS(encode_hello(hello, limits, payload));
  HelloMessage hello_out;
  HCR_CHECK_STATUS(decode_hello(payload.data(), payload.size(), limits, hello_out));
  HCR_CHECK_EQ(hello_out.nonce, hello.nonce);
  HCR_CHECK_EQ(hello_out.agent, hello.agent);

  WelcomeMessage welcome;
  welcome.protocol_version = protocol_version();
  welcome.epoch = CoordinatorEpoch{5};
  welcome.coordinator_name = "test";
  HCR_CHECK_STATUS(encode_welcome(welcome, limits, payload));
  WelcomeMessage welcome_out;
  HCR_CHECK_STATUS(decode_welcome(payload.data(), payload.size(), limits, welcome_out));
  HCR_CHECK(welcome_out.epoch == welcome.epoch);

  QueryRequest request;
  request.kind = QueryKind::DeviceCapabilities;
  request.device_capabilities.device = DeviceId::parse("dev.test");
  request.device_capabilities.only_current = false;
  request.device_capabilities.environment.set_driver_version("580.0");
  request.device_capabilities.capability_prefix = "cap.compute.";
  HCR_CHECK_STATUS(encode_query_request(request, limits, payload));
  QueryRequest request_out;
  HCR_CHECK_STATUS(decode_query_request(payload.data(), payload.size(), limits, request_out));
  HCR_CHECK(request_out.kind == QueryKind::DeviceCapabilities);
  HCR_CHECK(request_out.device_capabilities.device == request.device_capabilities.device);
  HCR_CHECK_EQ(request_out.device_capabilities.only_current, false);
  HCR_CHECK_EQ(request_out.device_capabilities.capability_prefix, std::string("cap.compute."));
  HCR_CHECK(request_out.device_capabilities.environment.has(env_keys::kDriverVersion));

  QueryResponse response;
  response.kind = QueryKind::Capability;
  response.capability.device = DeviceId::parse("dev.test");
  response.capability.capability = CapabilityId::parse("cap.compute.fp32");
  response.capability.declared_support = SupportState::SupportedNative;
  response.capability.effective_support = EffectiveSupport::Supported;
  response.capability.truth = TruthClass::Real;
  response.capability.explanation = "explanation";
  HCR_CHECK_STATUS(encode_query_response(response, limits, payload));
  QueryResponse response_out;
  HCR_CHECK_STATUS(decode_query_response(payload.data(), payload.size(), limits, response_out));
  HCR_CHECK(response_out.capability.device == response.capability.device);
  HCR_CHECK(response_out.capability.declared_support == SupportState::SupportedNative);
  HCR_CHECK_EQ(response_out.capability.explanation, std::string("explanation"));

  ShutdownMessage shutdown;
  shutdown.reason = "test shutdown";
  HCR_CHECK_STATUS(encode_shutdown(shutdown, limits, payload));
  ShutdownMessage shutdown_out;
  HCR_CHECK_STATUS(decode_shutdown(payload.data(), payload.size(), limits, shutdown_out));
  HCR_CHECK_EQ(shutdown_out.reason, shutdown.reason);

  FencePublisherMessage fence;
  fence.publisher = PublisherId::parse("publisher.test");
  fence.reason = "test";
  HCR_CHECK_STATUS(encode_fence_publisher(fence, limits, payload));
  FencePublisherMessage fence_out;
  HCR_CHECK_STATUS(decode_fence_publisher(payload.data(), payload.size(), limits, fence_out));
  HCR_CHECK(fence_out.publisher == fence.publisher);
}

HCR_TEST(codec, checksum_detects_single_bit_corruption) {
  const std::string text = "hardware capability registry";
  const std::uint32_t baseline =
      crc32(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    std::string mutated = text;
    mutated[i] = static_cast<char>(mutated[i] ^ 0x01);
    HCR_CHECK(crc32(reinterpret_cast<const std::uint8_t*>(mutated.data()), mutated.size()) != baseline);
  }
}
