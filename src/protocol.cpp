// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/protocol.hpp"

#include <algorithm>
#include <cstring>

#include "hcr/canonical.hpp"
#include "hcr/detail/codec.hpp"
#include "hcr/render.hpp"
#include "hcr/transport.hpp"
#include "hcr/version.hpp"

namespace hcr {

const char* to_string(MessageType type) noexcept {
  switch (type) {
    case MessageType::Hello: return "HELLO";
    case MessageType::Welcome: return "WELCOME";
    case MessageType::RegisterPublisher: return "REGISTER_PUBLISHER";
    case MessageType::PublisherAccepted: return "PUBLISHER_ACCEPTED";
    case MessageType::Publication: return "PUBLICATION";
    case MessageType::PublicationAccepted: return "PUBLICATION_ACCEPTED";
    case MessageType::QueryRequest: return "QUERY_REQUEST";
    case MessageType::QueryResponse: return "QUERY_RESPONSE";
    case MessageType::FencePublisher: return "FENCE_PUBLISHER";
    case MessageType::FenceAccepted: return "FENCE_ACCEPTED";
    case MessageType::SnapshotRequest: return "SNAPSHOT_REQUEST";
    case MessageType::SnapshotResponse: return "SNAPSHOT_RESPONSE";
    case MessageType::ShutdownRequest: return "SHUTDOWN_REQUEST";
    case MessageType::ShutdownAccepted: return "SHUTDOWN_ACCEPTED";
    case MessageType::ErrorResponse: return "ERROR_RESPONSE";
    case MessageType::Heartbeat: return "HEARTBEAT";
    case MessageType::HeartbeatAccepted: return "HEARTBEAT_ACCEPTED";
  }
  return "UNKNOWN";
}

bool message_type_from_string(std::string_view text, MessageType& out) noexcept {
  for (std::uint16_t i = 1; i <= static_cast<std::uint16_t>(MessageType::HeartbeatAccepted); ++i) {
    const auto type = static_cast<MessageType>(i);
    if (equals_ascii_ci(text, to_string(type))) {
      out = type;
      return true;
    }
  }
  return false;
}

bool message_type_known(std::uint16_t raw) noexcept {
  return raw >= static_cast<std::uint16_t>(MessageType::Hello) &&
         raw <= static_cast<std::uint16_t>(MessageType::HeartbeatAccepted);
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
  static std::uint32_t table[256];
  static bool initialized = false;
  if (!initialized) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) != 0u ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      }
      table[i] = value;
    }
    initialized = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

Status encode_frame(const Frame& frame, const RegistryLimits& limits, std::vector<std::uint8_t>& out) {
  if (!message_type_known(static_cast<std::uint16_t>(frame.type))) {
    return Status::failure(ErrorCode::ProtocolError, "refusing to encode an unknown message type");
  }
  if (frame.payload.size() > limits.max_frame_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "frame payload exceeds the configured bound");
  }
  if ((frame.flags & ~kKnownFrameFlags) != 0u) {
    return Status::failure(ErrorCode::ProtocolError, "refusing to encode undefined frame flags");
  }
  out.clear();
  out.reserve(kFrameHeaderSize + frame.payload.size());
  detail::ByteWriter writer(out);
  writer.u32(kFrameMagic);
  writer.u16(static_cast<std::uint16_t>(protocol_version()));
  writer.u16(static_cast<std::uint16_t>(frame.type));
  writer.u32(frame.flags);
  writer.u32(static_cast<std::uint32_t>(frame.payload.size()));
  writer.u32(crc32(frame.payload.data(), frame.payload.size()));
  writer.u64(frame.correlation);
  if (!frame.payload.empty()) writer.raw(frame.payload.data(), frame.payload.size());
  return Status::success();
}

FrameDecoder::FrameDecoder(RegistryLimits limits) : limits_(limits) { buffer_.reserve(4096); }

void FrameDecoder::reset() { buffer_.clear(); }

Status FrameDecoder::finish() const {
  if (buffer_.empty()) return Status::success();
  return Status::failure(ErrorCode::ProtocolError,
                         "connection ended with " + std::to_string(buffer_.size()) +
                             " bytes of an incomplete frame");
}

Status FrameDecoder::feed(const std::uint8_t* data, std::size_t size, std::vector<Frame>& out) {
  if (buffer_.size() + size > limits_.max_frame_bytes + kFrameHeaderSize) {
    return Status::failure(ErrorCode::LimitExceeded, "frame buffer would exceed the configured bound");
  }
  if (size != 0) buffer_.insert(buffer_.end(), data, data + size);
  std::size_t consumed = 0;
  while (buffer_.size() - consumed >= kFrameHeaderSize) {
    detail::ByteReader header(buffer_.data() + consumed, kFrameHeaderSize);
    const std::uint32_t magic = header.u32();
    const std::uint16_t version = header.u16();
    const std::uint16_t raw_type = header.u16();
    const std::uint32_t flags = header.u32();
    const std::uint32_t length = header.u32();
    const std::uint32_t checksum = header.u32();
    const std::uint64_t correlation = header.u64();
    if (magic != kFrameMagic) {
      return Status::failure(ErrorCode::ProtocolError, "frame magic does not match");
    }
    if (version != static_cast<std::uint16_t>(protocol_version())) {
      return Status::failure(ErrorCode::UnsupportedVersion,
                             "frame declares protocol version " + std::to_string(version));
    }
    if (!message_type_known(raw_type)) {
      return Status::failure(ErrorCode::ProtocolError, "frame declares an unknown message type");
    }
    if ((flags & ~kKnownFrameFlags) != 0u) {
      return Status::failure(ErrorCode::ProtocolError, "frame declares undefined flags");
    }
    if (length > limits_.max_frame_bytes) {
      return Status::failure(ErrorCode::LimitExceeded, "frame payload length exceeds the configured bound");
    }
    if (buffer_.size() - consumed < kFrameHeaderSize + static_cast<std::size_t>(length)) break;
    const std::uint8_t* payload = buffer_.data() + consumed + kFrameHeaderSize;
    if (crc32(payload, length) != checksum) {
      return Status::failure(ErrorCode::IntegrityFailure, "frame checksum does not match its payload");
    }
    Frame frame;
    frame.type = static_cast<MessageType>(raw_type);
    frame.flags = flags;
    frame.correlation = correlation;
    frame.payload.assign(payload, payload + length);
    out.push_back(std::move(frame));
    consumed += kFrameHeaderSize + static_cast<std::size_t>(length);
  }
  if (consumed != 0) buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
  return Status::success();
}

Status send_frame(TcpConnection& connection, const Frame& frame, const RegistryLimits& limits) {
  std::vector<std::uint8_t> bytes;
  const Status encoded = encode_frame(frame, limits, bytes);
  if (!encoded.ok()) return encoded;
  return connection.send_all(bytes.data(), bytes.size());
}

Status receive_frame(TcpConnection& connection, FrameDecoder& decoder, Frame& out) {
  std::vector<Frame> frames;
  std::uint8_t buffer[16384];
  while (true) {
    frames.clear();
    const Status fed = decoder.feed(nullptr, 0, frames);
    if (!fed.ok()) return fed;
    if (!frames.empty()) {
      out = std::move(frames.front());
      return Status::success();
    }
    std::size_t received = 0;
    const Status status = connection.recv_some(buffer, sizeof(buffer), received);
    if (!status.ok()) return status;
    if (received == 0) {
      const Status trailing = decoder.finish();
      if (!trailing.ok()) return trailing;
      return Status::failure(ErrorCode::IoFailure, "peer closed the connection");
    }
    const Status append = decoder.feed(buffer, received, frames);
    if (!append.ok()) return append;
    if (!frames.empty()) {
      out = std::move(frames.front());
      return Status::success();
    }
  }
}

Status encode_publication(const Publication& publication, const RegistryLimits& limits,
                          std::vector<std::uint8_t>& out) {
  // The codec enforces bounds; semantic validation stays the registry's job so
  // a malformed publication is rejected on arrival. A field that would have to
  // be truncated is refused outright: the registry never receives silently
  // shortened data.
  out.clear();
  detail::ByteWriter writer(out);
  detail::write_publication(writer, publication, limits);
  if (writer.truncated()) {
    return Status::failure(ErrorCode::LimitExceeded,
                           "publication carries a field longer than the configured bound");
  }
  if (out.size() > limits.max_frame_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "publication exceeds the frame bound");
  }
  return Status::success();
}

Status decode_publication(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                          Publication& out) {
  detail::ByteReader reader(data, size);
  out = detail::read_publication(reader, limits);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "publication payload carries trailing bytes");
  }
  return Status::success();
}

Status encode_hello(const HelloMessage& message, const RegistryLimits& limits, std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u32(message.protocol_version);
  writer.str(message.agent, limits.max_metadata_bytes);
  writer.str(message.role, limits.max_metadata_bytes);
  writer.u64(message.nonce);
  return Status::success();
}

Status decode_hello(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                    HelloMessage& out) {
  detail::ByteReader reader(data, size);
  out = HelloMessage{};
  out.protocol_version = reader.u32();
  out.agent = reader.str(limits.max_metadata_bytes);
  out.role = reader.str(limits.max_metadata_bytes);
  out.nonce = reader.u64();
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "hello carries trailing bytes");
  return Status::success();
}

Status encode_welcome(const WelcomeMessage& message, const RegistryLimits& limits,
                      std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u32(message.protocol_version);
  writer.u64(message.epoch.value());
  writer.str(message.coordinator_name, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_welcome(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                      WelcomeMessage& out) {
  detail::ByteReader reader(data, size);
  out = WelcomeMessage{};
  out.protocol_version = reader.u32();
  out.epoch = CoordinatorEpoch{reader.u64()};
  out.coordinator_name = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "welcome carries trailing bytes");
  return Status::success();
}

Status encode_register_publisher(const RegisterPublisherMessage& message, const RegistryLimits& limits,
                                 std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  detail::write_publisher_identity(writer, message.identity);
  writer.str(message.adapter, limits.max_metadata_bytes);
  writer.str(message.agent, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_register_publisher(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                                 RegisterPublisherMessage& out) {
  detail::ByteReader reader(data, size);
  out = RegisterPublisherMessage{};
  out.identity = detail::read_publisher_identity(reader);
  out.adapter = reader.str(limits.max_metadata_bytes);
  out.agent = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "register publisher carries trailing bytes");
  }
  return Status::success();
}

Status encode_publisher_accepted(const PublisherAcceptedMessage& message, const RegistryLimits& limits,
                                 std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.code));
  writer.u64(message.epoch.value());
  detail::write_publisher_identity(writer, message.identity);
  writer.str(message.detail, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_publisher_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                                 PublisherAcceptedMessage& out) {
  detail::ByteReader reader(data, size);
  out = PublisherAcceptedMessage{};
  const std::uint16_t code = reader.u16();
  if (code > static_cast<std::uint16_t>(ApplyCode::RejectedInvariant)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown apply code in publisher acceptance");
  }
  out.code = static_cast<ApplyCode>(code);
  out.epoch = CoordinatorEpoch{reader.u64()};
  out.identity = detail::read_publisher_identity(reader);
  out.detail = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "publisher acceptance carries trailing bytes");
  }
  return Status::success();
}

Status encode_publication_accepted(const PublicationAcceptedMessage& message, const RegistryLimits& limits,
                                   std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.code));
  writer.str(message.detail, limits.max_metadata_bytes);
  writer.u64(message.device_generation.value());
  writer.u64(message.device_boot.value());
  return Status::success();
}

Status decode_publication_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                                   PublicationAcceptedMessage& out) {
  detail::ByteReader reader(data, size);
  out = PublicationAcceptedMessage{};
  const std::uint16_t code = reader.u16();
  if (code > static_cast<std::uint16_t>(ApplyCode::RejectedInvariant)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown apply code in publication acceptance");
  }
  out.code = static_cast<ApplyCode>(code);
  out.detail = reader.str(limits.max_metadata_bytes);
  out.device_generation = DeviceGeneration{reader.u64()};
  out.device_boot = DeviceBootId{reader.u64()};
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "publication acceptance carries trailing bytes");
  }
  return Status::success();
}

const char* to_string(QueryKind kind) noexcept {
  switch (kind) {
    case QueryKind::Capability: return "capability";
    case QueryKind::DeviceCapabilities: return "device-capabilities";
    case QueryKind::Fleet: return "fleet";
    case QueryKind::StaleCapabilities: return "stale-capabilities";
    case QueryKind::Quirks: return "quirks";
    case QueryKind::QuirkRecords: return "quirk-records";
    case QueryKind::Compatibility: return "compatibility";
    case QueryKind::Compare: return "compare";
    case QueryKind::Devices: return "devices";
    case QueryKind::Publishers: return "publishers";
    case QueryKind::Snapshot: return "snapshot";
    case QueryKind::Explain: return "explain";
  }
  return "capability";
}

bool query_kind_from_string(std::string_view text, QueryKind& out) noexcept {
  for (std::uint16_t i = 1; i <= static_cast<std::uint16_t>(QueryKind::Explain); ++i) {
    const auto kind = static_cast<QueryKind>(i);
    if (equals_ascii_ci(text, to_string(kind))) {
      out = kind;
      return true;
    }
  }
  return false;
}

Status encode_query_request(const QueryRequest& message, const RegistryLimits& limits,
                            std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.kind));
  switch (message.kind) {
    case QueryKind::Capability:
    case QueryKind::Explain:
      detail::write_capability_query(writer, message.capability, limits);
      break;
    case QueryKind::DeviceCapabilities:
      detail::write_device_capability_query(writer, message.device_capabilities, limits);
      break;
    case QueryKind::Fleet:
      detail::write_fleet_query(writer, message.fleet, limits);
      break;
    case QueryKind::StaleCapabilities:
      detail::write_stale_query(writer, message.stale, limits);
      break;
    case QueryKind::Quirks:
      detail::write_quirk_query(writer, message.quirks, limits);
      break;
    case QueryKind::Compatibility:
      detail::write_compatibility_query(writer, message.compatibility, limits);
      break;
    case QueryKind::Compare:
      detail::write_comparison_request(writer, message.comparison, limits);
      break;
    case QueryKind::Devices: {
      detail::ByteWriter& w = writer;
      w.str(message.device.str(), kMaxTokenLength);
      break;
    }
    case QueryKind::Publishers:
      detail::write_publisher_query(writer, message.publishers, limits);
      break;
    case QueryKind::QuirkRecords:
    case QueryKind::Snapshot: {
      writer.u64(0);
      break;
    }
  }
  return Status::success();
}

Status decode_query_request(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                            QueryRequest& out) {
  detail::ByteReader reader(data, size);
  out = QueryRequest{};
  const std::uint16_t kind = reader.u16();
  if (kind < 1 || kind > static_cast<std::uint16_t>(QueryKind::Explain)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown query kind on the wire");
  }
  out.kind = static_cast<QueryKind>(kind);
  switch (out.kind) {
    case QueryKind::Capability:
    case QueryKind::Explain:
      out.capability = detail::read_capability_query(reader, limits);
      break;
    case QueryKind::DeviceCapabilities:
      out.device_capabilities = detail::read_device_capability_query(reader, limits);
      break;
    case QueryKind::Fleet:
      out.fleet = detail::read_fleet_query(reader, limits);
      break;
    case QueryKind::StaleCapabilities:
      out.stale = detail::read_stale_query(reader, limits);
      break;
    case QueryKind::Quirks:
      out.quirks = detail::read_quirk_query(reader, limits);
      break;
    case QueryKind::Compatibility:
      out.compatibility = detail::read_compatibility_query(reader, limits);
      break;
    case QueryKind::Compare:
      out.comparison = detail::read_comparison_request(reader, limits);
      break;
    case QueryKind::Devices:
      out.device = DeviceId::parse(reader.str(kMaxTokenLength));
      break;
    case QueryKind::Publishers:
      out.publishers = detail::read_publisher_query(reader, limits);
      break;
    case QueryKind::QuirkRecords:
    case QueryKind::Snapshot:
      (void)reader.u64();
      break;
  }
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "query request carries trailing bytes");
  }
  return Status::success();
}

Status encode_query_response(const QueryResponse& message, const RegistryLimits& limits,
                             std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.code));
  writer.str(message.message, limits.max_string_bytes);
  writer.u16(static_cast<std::uint16_t>(message.kind));
  switch (message.kind) {
    case QueryKind::Capability:
    case QueryKind::Explain:
      detail::write_resolved_capability(writer, message.capability, limits);
      break;
    case QueryKind::DeviceCapabilities:
    case QueryKind::Fleet:
    case QueryKind::StaleCapabilities: {
      const std::size_t count = std::min(message.capabilities.size(), limits.max_query_results);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) {
        detail::write_resolved_capability(writer, message.capabilities[i], limits);
      }
      break;
    }
    case QueryKind::Quirks: {
      const std::size_t count = std::min(message.quirks.size(), limits.max_query_results);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) detail::write_quirk_application(writer, message.quirks[i], limits);
      break;
    }
    case QueryKind::QuirkRecords: {
      const std::size_t count = std::min(message.quirk_records.size(), limits.max_quirks);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) detail::write_quirk_record(writer, message.quirk_records[i], limits);
      break;
    }
    case QueryKind::Compatibility: {
      const std::size_t count = std::min(message.compatibility.size(), limits.max_compatibility_facts);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) {
        detail::write_compatibility_fact(writer, message.compatibility[i], limits);
      }
      break;
    }
    case QueryKind::Compare:
      detail::write_device_comparison(writer, message.comparison, limits);
      break;
    case QueryKind::Devices: {
      const std::size_t count = std::min(message.subjects.size(), limits.max_devices);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) detail::write_subject_snapshot(writer, message.subjects[i], limits);
      break;
    }
    case QueryKind::Publishers: {
      const std::size_t count = std::min(message.publishers.size(), limits.max_publishers);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) detail::write_publisher_record(writer, message.publishers[i], limits);
      break;
    }
    case QueryKind::Snapshot:
      detail::write_registry_snapshot(writer, message.snapshot, limits);
      break;
  }
  writer.str(message.text, limits.max_string_bytes);
  if (writer.truncated()) {
    return Status::failure(ErrorCode::LimitExceeded,
                           "query response carries a field longer than the configured bound");
  }
  if (out.size() > limits.max_frame_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "query response exceeds the frame bound");
  }
  return Status::success();
}

Status decode_query_response(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                             QueryResponse& out) {
  detail::ByteReader reader(data, size);
  out = QueryResponse{};
  const std::uint16_t code = reader.u16();
  if (code > static_cast<std::uint16_t>(ErrorCode::Internal)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown error code in query response");
  }
  out.code = static_cast<ErrorCode>(code);
  out.message = reader.str(limits.max_string_bytes);
  const std::uint16_t kind = reader.u16();
  if (kind < 1 || kind > static_cast<std::uint16_t>(QueryKind::Explain)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown query kind in query response");
  }
  out.kind = static_cast<QueryKind>(kind);
  const auto read_count = [&reader, &limits](std::size_t bound, const char* what) -> std::uint32_t {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) return 0;
    if (count > bound || count > reader.remaining()) {
      reader.fail(ErrorCode::LimitExceeded, std::string(what) + " count exceeds the bound");
      return 0;
    }
    return count;
  };
  switch (out.kind) {
    case QueryKind::Capability:
    case QueryKind::Explain:
      out.capability = detail::read_resolved_capability(reader, limits);
      break;
    case QueryKind::DeviceCapabilities:
    case QueryKind::Fleet:
    case QueryKind::StaleCapabilities: {
      const std::uint32_t count = read_count(limits.max_query_results, "capability result");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.capabilities.push_back(detail::read_resolved_capability(reader, limits));
      }
      break;
    }
    case QueryKind::Quirks: {
      const std::uint32_t count = read_count(limits.max_query_results, "quirk application");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.quirks.push_back(detail::read_quirk_application(reader, limits));
      }
      break;
    }
    case QueryKind::QuirkRecords: {
      const std::uint32_t count = read_count(limits.max_quirks, "quirk record");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.quirk_records.push_back(detail::read_quirk_record(reader, limits));
      }
      break;
    }
    case QueryKind::Compatibility: {
      const std::uint32_t count = read_count(limits.max_compatibility_facts, "compatibility fact");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.compatibility.push_back(detail::read_compatibility_fact(reader, limits));
      }
      break;
    }
    case QueryKind::Compare:
      out.comparison = detail::read_device_comparison(reader, limits);
      break;
    case QueryKind::Devices: {
      const std::uint32_t count = read_count(limits.max_devices, "subject");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.subjects.push_back(detail::read_subject_snapshot(reader, limits));
      }
      break;
    }
    case QueryKind::Publishers: {
      const std::uint32_t count = read_count(limits.max_publishers, "publisher");
      for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
        out.publishers.push_back(detail::read_publisher_record(reader, limits));
      }
      break;
    }
    case QueryKind::Snapshot:
      out.snapshot = detail::read_registry_snapshot(reader, limits);
      break;
  }
  out.text = reader.str(limits.max_string_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "query response carries trailing bytes");
  }
  return Status::success();
}

Status encode_fence_publisher(const FencePublisherMessage& message, const RegistryLimits& limits,
                              std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.str(message.publisher.str(), kMaxTokenLength);
  writer.str(message.reason, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_fence_publisher(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                              FencePublisherMessage& out) {
  detail::ByteReader reader(data, size);
  out = FencePublisherMessage{};
  out.publisher = PublisherId::parse(reader.str(kMaxTokenLength));
  out.reason = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "fence carries trailing bytes");
  return Status::success();
}

Status encode_fence_accepted(const FenceAcceptedMessage& message, const RegistryLimits& limits,
                             std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.code));
  writer.str(message.detail, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_fence_accepted(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                             FenceAcceptedMessage& out) {
  detail::ByteReader reader(data, size);
  out = FenceAcceptedMessage{};
  const std::uint16_t code = reader.u16();
  if (code > static_cast<std::uint16_t>(ApplyCode::RejectedInvariant)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown apply code in fence acceptance");
  }
  out.code = static_cast<ApplyCode>(code);
  out.detail = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::ProtocolError, "fence acceptance carries trailing bytes");
  }
  return Status::success();
}

Status encode_shutdown(const ShutdownMessage& message, const RegistryLimits& limits,
                       std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.str(message.reason, limits.max_metadata_bytes);
  return Status::success();
}

Status decode_shutdown(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                       ShutdownMessage& out) {
  detail::ByteReader reader(data, size);
  out = ShutdownMessage{};
  out.reason = reader.str(limits.max_metadata_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "shutdown carries trailing bytes");
  return Status::success();
}

Status encode_error_message(const ErrorMessage& message, const RegistryLimits& limits,
                            std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u16(static_cast<std::uint16_t>(message.code));
  writer.str(message.message, limits.max_string_bytes);
  return Status::success();
}

Status decode_error_message(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                            ErrorMessage& out) {
  detail::ByteReader reader(data, size);
  out = ErrorMessage{};
  const std::uint16_t code = reader.u16();
  if (code > static_cast<std::uint16_t>(ErrorCode::Internal)) {
    return Status::failure(ErrorCode::ProtocolError, "unknown error code on the wire");
  }
  out.code = static_cast<ErrorCode>(code);
  out.message = reader.str(limits.max_string_bytes);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "error carries trailing bytes");
  return Status::success();
}

Status encode_heartbeat(const HeartbeatMessage& message, const RegistryLimits& limits,
                        std::vector<std::uint8_t>& out) {
  out.clear();
  detail::ByteWriter writer(out);
  writer.u64(message.sequence);
  detail::write_publisher_identity(writer, message.identity);
  (void)limits;
  return Status::success();
}

Status decode_heartbeat(const std::uint8_t* data, std::size_t size, const RegistryLimits& limits,
                        HeartbeatMessage& out) {
  detail::ByteReader reader(data, size);
  out = HeartbeatMessage{};
  out.sequence = reader.u64();
  out.identity = detail::read_publisher_identity(reader);
  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) return Status::failure(ErrorCode::ProtocolError, "heartbeat carries trailing bytes");
  (void)limits;
  return Status::success();
}

}  // namespace hcr
