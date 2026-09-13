// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/publisher.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "hcr/render.hpp"
#include "hcr/time.hpp"

namespace hcr {

struct CapabilityPublisher::Impl {
  explicit Impl(PublisherConfig publisher_config)
      : config(std::move(publisher_config)), decoder(config.limits) {}

  PublisherConfig config;
  TcpConnection connection;
  FrameDecoder decoder;
  PublisherIdentity identity;
  CoordinatorEpoch epoch;
  std::uint64_t sequence = 0;
  EvidenceGeneration evidence_generation = EvidenceGeneration::first();
  std::uint64_t correlation = 0;
  bool registered = false;

  Status send(Frame& frame) {
    frame.correlation = ++correlation;
    return send_frame(connection, frame, config.limits);
  }

  Status receive(MessageType expected, Frame& out) {
    const Status status = receive_frame(connection, decoder, out);
    if (!status.ok()) return status;
    if (out.type == MessageType::ErrorResponse) {
      ErrorMessage error;
      const Status decoded =
          decode_error_message(out.payload.data(), out.payload.size(), config.limits, error);
      if (!decoded.ok()) return decoded;
      return Status::failure(error.code, error.message);
    }
    if (out.type != expected) {
      return Status::failure(ErrorCode::ProtocolError,
                             std::string("expected ") + to_string(expected) + " but received " +
                                 to_string(out.type));
    }
    return Status::success();
  }
};

CapabilityPublisher::CapabilityPublisher(PublisherConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CapabilityPublisher::~CapabilityPublisher() {
  if (impl_ && impl_->connection.valid()) {
    impl_->connection.close();
  }
}

Status CapabilityPublisher::connect_and_register() {
  if (!impl_->config.publisher.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "publisher requires a canonical publisher identity");
  }
  const Status connected = connect_loopback(impl_->config.port, impl_->connection);
  if (!connected.ok()) return connected;

  HelloMessage hello;
  hello.protocol_version = protocol_version();
  hello.agent = impl_->config.agent;
  hello.role = "publisher";
  hello.nonce = impl_->config.nonce;
  std::vector<std::uint8_t> payload;
  Status encoded = encode_hello(hello, impl_->config.limits, payload);
  if (!encoded.ok()) return encoded;
  Frame frame;
  frame.type = MessageType::Hello;
  frame.payload = std::move(payload);
  Status status = impl_->send(frame);
  if (!status.ok()) return status;
  Frame response;
  status = impl_->receive(MessageType::Welcome, response);
  if (!status.ok()) return status;
  WelcomeMessage welcome;
  status = decode_welcome(response.payload.data(), response.payload.size(), impl_->config.limits, welcome);
  if (!status.ok()) return status;
  if (welcome.protocol_version != protocol_version()) {
    return Status::failure(ErrorCode::UnsupportedVersion,
                           "coordinator protocol version " + std::to_string(welcome.protocol_version) +
                               " is not supported");
  }
  impl_->epoch = welcome.epoch;

  RegisterPublisherMessage registration;
  registration.identity.id = impl_->config.publisher;
  registration.adapter = impl_->config.adapter;
  registration.agent = impl_->config.agent;
  encoded = encode_register_publisher(registration, impl_->config.limits, payload);
  if (!encoded.ok()) return encoded;
  frame = Frame{};
  frame.type = MessageType::RegisterPublisher;
  frame.payload = std::move(payload);
  status = impl_->send(frame);
  if (!status.ok()) return status;
  status = impl_->receive(MessageType::PublisherAccepted, response);
  if (!status.ok()) return status;
  PublisherAcceptedMessage accepted;
  status = decode_publisher_accepted(response.payload.data(), response.payload.size(), impl_->config.limits,
                                     accepted);
  if (!status.ok()) return status;
  if (!apply_code_accepted(accepted.code)) {
    return Status::failure(ErrorCode::UnknownPublisher,
                           std::string("publisher registration rejected: ") + to_string(accepted.code) +
                               ": " + accepted.detail);
  }
  impl_->identity = accepted.identity;
  impl_->epoch = accepted.epoch;
  impl_->registered = true;
  return Status::success();
}

ApplyResult CapabilityPublisher::ensure_publisher(const PublisherIdentity& identity, std::string adapter) {
  ApplyResult result;
  if (!impl_->registered) {
    const Status status = connect_and_register();
    if (!status.ok()) {
      result.code = ApplyCode::RejectedUnknownPublisher;
      result.detail = status.describe();
      return result;
    }
  }
  if (identity.id.valid() && identity.id != impl_->identity.id) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = "publisher is already registered as " + impl_->identity.id.str();
    return result;
  }
  (void)adapter;
  result.code = ApplyCode::Accepted;
  result.mutated = false;
  result.assigned_publisher = impl_->identity;
  result.detail = "publisher identity confirmed by the coordinator";
  return result;
}

std::uint64_t CapabilityPublisher::next_sequence(const PublisherId& publisher) {
  (void)publisher;
  return ++impl_->sequence;
}

ApplyResult CapabilityPublisher::publish(const Publication& publication) {
  ApplyResult result;
  if (!impl_->registered) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = "publisher is not registered with a coordinator";
    return result;
  }
  Publication bound = publication;
  bound.publisher = impl_->identity;
  bound.sequence = next_sequence();
  if (!bound.evidence_generation.valid()) bound.evidence_generation = impl_->evidence_generation;
  if (bound.issued_at_utc.empty()) bound.issued_at_utc = utc_now_iso8601();

  std::vector<std::uint8_t> payload;
  const Status encoded = encode_publication(bound, impl_->config.limits, payload);
  if (!encoded.ok()) {
    result.code = ApplyCode::RejectedValidation;
    result.detail = encoded.describe();
    return result;
  }
  Frame frame;
  frame.type = MessageType::Publication;
  frame.payload = std::move(payload);
  const Status sent = impl_->send(frame);
  if (!sent.ok()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = sent.describe();
    return result;
  }
  Frame response;
  const Status received = impl_->receive(MessageType::PublicationAccepted, response);
  if (!received.ok()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = received.describe();
    return result;
  }
  PublicationAcceptedMessage accepted;
  const Status decoded = decode_publication_accepted(response.payload.data(), response.payload.size(),
                                                     impl_->config.limits, accepted);
  if (!decoded.ok()) {
    result.code = ApplyCode::RejectedValidation;
    result.detail = decoded.describe();
    return result;
  }
  result.code = accepted.code;
  result.detail = accepted.detail;
  result.mutated = accepted.code == ApplyCode::Accepted;
  // The coordinator is the authority for generation assignment; the value it
  // returns is what later publications must bind to.
  result.device_generation = accepted.device_generation.valid() ? accepted.device_generation
                                                                : bound.capability.device_generation;
  result.device_boot = accepted.device_boot.valid() ? accepted.device_boot : bound.capability.device_boot;
  return result;
}

ApplyResult CapabilityPublisher::heartbeat() {
  ApplyResult result;
  if (!impl_->registered) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = "publisher is not registered with a coordinator";
    return result;
  }
  HeartbeatMessage message;
  message.sequence = next_sequence();
  message.identity = impl_->identity;
  std::vector<std::uint8_t> payload;
  const Status encoded = encode_heartbeat(message, impl_->config.limits, payload);
  if (!encoded.ok()) {
    result.code = ApplyCode::RejectedValidation;
    result.detail = encoded.describe();
    return result;
  }
  Frame frame;
  frame.type = MessageType::Heartbeat;
  frame.payload = std::move(payload);
  const Status sent = impl_->send(frame);
  if (!sent.ok()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = sent.describe();
    impl_->registered = false;
    return result;
  }
  Frame response;
  const Status received = impl_->receive(MessageType::HeartbeatAccepted, response);
  if (!received.ok()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = received.describe();
    impl_->registered = false;
    return result;
  }
  result.code = ApplyCode::Accepted;
  result.detail = "heartbeat acknowledged";
  result.assigned_publisher = impl_->identity;
  return result;
}

Status CapabilityPublisher::disconnect() { return impl_->connection.close(); }

bool CapabilityPublisher::connected() const noexcept {
  return impl_ && impl_->connection.valid() && impl_->registered;
}

PublisherIdentity CapabilityPublisher::identity() const { return impl_->identity; }

CoordinatorEpoch CapabilityPublisher::epoch() const noexcept { return impl_->epoch; }

std::uint64_t CapabilityPublisher::next_sequence() noexcept { return ++impl_->sequence; }

EvidenceGeneration CapabilityPublisher::next_evidence_generation() noexcept {
  impl_->evidence_generation = impl_->evidence_generation.valid() ? impl_->evidence_generation.next()
                                                                 : EvidenceGeneration::first();
  return impl_->evidence_generation;
}

}  // namespace hcr
