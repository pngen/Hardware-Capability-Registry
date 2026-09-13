// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hcr/compatibility.hpp"
#include "hcr/discovery.hpp"
#include "hcr/durable_state.hpp"
#include "hcr/limits.hpp"
#include "hcr/publication.hpp"
#include "hcr/query.hpp"
#include "hcr/registry.hpp"
#include "hcr/snapshot.hpp"
#include "hcr/status.hpp"
#include "hcr/version.hpp"

namespace hcr {

/// Bounded, versioned, checksum-protected framing.
///
///   offset  size  field
///   0       4     magic 'H','C','R','P'
///   4       2     protocol version (major)
///   6       2     message type
///   8       4     flags (only defined bits accepted)
///   12      4     payload length (bounded by RegistryLimits::max_frame_bytes)
///   16      4     CRC-32 of the payload
///   20      8     correlation id
///   28      ...   payload
///
/// Every field is validated before a payload is interpreted. A frame that
/// fails validation is rejected before it can mutate anything.
inline constexpr std::size_t kFrameHeaderSize = 28;
inline constexpr std::uint32_t kFrameMagic = 0x50524348u;  // 'HCRP' little-endian
inline constexpr std::uint32_t kFrameFlagControl = 1u << 0;
inline constexpr std::uint32_t kFrameFlagAck = 1u << 1;
inline constexpr std::uint32_t kKnownFrameFlags = kFrameFlagControl | kFrameFlagAck;

enum class MessageType : std::uint16_t {
  Hello = 1,
  Welcome = 2,
  RegisterPublisher = 3,
  PublisherAccepted = 4,
  Publication = 5,
  PublicationAccepted = 6,
  QueryRequest = 7,
  QueryResponse = 8,
  FencePublisher = 9,
  FenceAccepted = 10,
  SnapshotRequest = 11,
  SnapshotResponse = 12,
  ShutdownRequest = 13,
  ShutdownAccepted = 14,
  ErrorResponse = 15,
  Heartbeat = 16,
  HeartbeatAccepted = 17,
};

const char* to_string(MessageType type) noexcept;
bool message_type_from_string(std::string_view text, MessageType& out) noexcept;
bool message_type_known(std::uint16_t raw) noexcept;

struct Frame {
  MessageType type = MessageType::Hello;
  std::uint32_t flags = 0;
  std::uint64_t correlation = 0;
  std::vector<std::uint8_t> payload;
};

Status encode_frame(const Frame& frame, const RegistryLimits& limits, std::vector<std::uint8_t>& out);

/// Incremental frame decoder. Bytes are appended as they arrive from a socket;
/// complete frames are appended to the output vector. The decoder is bounded:
/// it refuses to buffer more than one maximal frame.
class FrameDecoder {
 public:
  explicit FrameDecoder(RegistryLimits limits);

  Status feed(const std::uint8_t* data, std::size_t size, std::vector<Frame>& out);
  /// Reports a truncated frame left in the buffer.
  Status finish() const;
  void reset();
  [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size(); }

 private:
  RegistryLimits limits_;
  std::vector<std::uint8_t> buffer_;
};

// ---------------------------------------------------------------------------
// Payload codecs. Each codec validates bounds and canonical form; decoding a
// malformed payload fails without producing a partially populated value.
// ---------------------------------------------------------------------------

struct HelloMessage {
  std::uint32_t protocol_version = 0;
  std::string agent;
  std::string role;
  std::uint64_t nonce = 0;
};

struct WelcomeMessage {
  std::uint32_t protocol_version = 0;
  CoordinatorEpoch epoch;
  std::string coordinator_name;
};

struct RegisterPublisherMessage {
  PublisherIdentity identity;
  std::string adapter;
  std::string agent;
};

struct PublisherAcceptedMessage {
  ApplyCode code = ApplyCode::RejectedValidation;
  CoordinatorEpoch epoch;
  PublisherIdentity identity;
  std::string detail;
};

struct PublicationMessage {
  Publication publication;
};

struct PublicationAcceptedMessage {
  ApplyCode code = ApplyCode::RejectedValidation;
  std::string detail;
  /// Generations the registry assigned to a device registration. A remote
  /// publisher cannot invent them, so the acceptance carries them back and the
  /// publisher binds subsequent capability evidence to exactly these values.
  DeviceGeneration device_generation;
  DeviceBootId device_boot;
};

enum class QueryKind : std::uint16_t {
  Capability = 1,
  DeviceCapabilities,
  Fleet,
  StaleCapabilities,
  Quirks,
  QuirkRecords,
  Compatibility,
  Compare,
  Devices,
  Publishers,
  Snapshot,
  Explain,
};

const char* to_string(QueryKind kind) noexcept;
bool query_kind_from_string(std::string_view text, QueryKind& out) noexcept;

struct QueryRequest {
  QueryKind kind = QueryKind::Capability;
  CapabilityQuery capability;
  DeviceCapabilityQuery device_capabilities;
  FleetCapabilityQuery fleet;
  StaleCapabilityQuery stale;
  QuirkQuery quirks;
  ComparisonRequest comparison;
  CompatibilityQuery compatibility;
  PublisherQuery publishers;
  DeviceId device;
  CapabilityId fleet_capability;
};

struct QueryResponse {
  ErrorCode code = ErrorCode::Ok;
  std::string message;
  QueryKind kind = QueryKind::Capability;
  ResolvedCapability capability;
  std::vector<ResolvedCapability> capabilities;
  DeviceComparison comparison;
  std::vector<QuirkApplication> quirks;
  std::vector<QuirkRecord> quirk_records;
  std::vector<CompatibilityFact> compatibility;
  std::vector<SubjectSnapshot> subjects;
  std::vector<PublisherRecord> publishers;
  RegistrySnapshot snapshot;
  std::string text;
};

struct FencePublisherMessage {
  PublisherId publisher;
  std::string reason;
};

struct FenceAcceptedMessage {
  ApplyCode code = ApplyCode::RejectedValidation;
  std::string detail;
};

struct ShutdownMessage {
  std::string reason;
};

struct ErrorMessage {
  ErrorCode code = ErrorCode::Internal;
  std::string message;
};

struct HeartbeatMessage {
  std::uint64_t sequence = 0;
  PublisherIdentity identity;
};

Status encode_publication(const Publication& publication, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_publication(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, Publication& out);

Status encode_hello(const HelloMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_hello(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, HelloMessage& out);
Status encode_welcome(const WelcomeMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_welcome(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, WelcomeMessage& out);
Status encode_register_publisher(const RegisterPublisherMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_register_publisher(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, RegisterPublisherMessage& out);
Status encode_publisher_accepted(const PublisherAcceptedMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_publisher_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, PublisherAcceptedMessage& out);
Status encode_publication_accepted(const PublicationAcceptedMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_publication_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, PublicationAcceptedMessage& out);
Status encode_query_request(const QueryRequest& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_query_request(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, QueryRequest& out);
Status encode_query_response(const QueryResponse& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_query_response(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, QueryResponse& out);
Status encode_fence_publisher(const FencePublisherMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_fence_publisher(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, FencePublisherMessage& out);
Status encode_fence_accepted(const FenceAcceptedMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_fence_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, FenceAcceptedMessage& out);
Status encode_shutdown(const ShutdownMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_shutdown(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, ShutdownMessage& out);
Status encode_error_message(const ErrorMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_error_message(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, ErrorMessage& out);
Status encode_heartbeat(const HeartbeatMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out);
Status decode_heartbeat(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits, HeartbeatMessage& out);

/// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320) used for frame and
/// state integrity.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept;

class TcpConnection;

/// Writes one complete frame.
Status send_frame(TcpConnection& connection, const Frame& frame, const RegistryLimits& limits);

/// Blocks until one complete frame arrives. Returns an IoFailure when the peer
/// closes first, and reports a truncated frame rather than silently dropping
/// it. No timeout is configured anywhere.
Status receive_frame(TcpConnection& connection, FrameDecoder& decoder, Frame& out);

}  // namespace hcr
