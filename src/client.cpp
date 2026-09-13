// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/client.hpp"

#include <string>
#include <utility>
#include <vector>

#include "hcr/protocol.hpp"
#include "hcr/transport.hpp"

namespace hcr {

struct CapabilityClient::Impl {
  explicit Impl(ClientConfig client_config) : config(std::move(client_config)), decoder(config.limits) {}

  ClientConfig config;
  TcpConnection connection;
  FrameDecoder decoder;
  CoordinatorEpoch epoch;
  std::uint64_t correlation = 0;

  Status exchange(const QueryRequest& request, QueryResponse& out) {
    if (!connection.valid()) {
      return Status::failure(ErrorCode::IoFailure, "client is not connected");
    }
    std::vector<std::uint8_t> payload;
    const Status encoded = encode_query_request(request, config.limits, payload);
    if (!encoded.ok()) return encoded;
    Frame frame;
    frame.type = MessageType::QueryRequest;
    frame.correlation = ++correlation;
    frame.payload = std::move(payload);
    const Status sent = send_frame(connection, frame, config.limits);
    if (!sent.ok()) return sent;
    while (true) {
      Frame response;
      const Status received = receive_frame(connection, decoder, response);
      if (!received.ok()) return received;
      if (response.correlation != frame.correlation) {
        return Status::failure(ErrorCode::ProtocolError, "query response correlation does not match");
      }
      if (response.type == MessageType::ErrorResponse) {
        ErrorMessage error;
        const Status decoded =
            decode_error_message(response.payload.data(), response.payload.size(), config.limits, error);
        if (!decoded.ok()) return decoded;
        return Status::failure(error.code, error.message);
      }
      if (response.type != MessageType::QueryResponse) {
        return Status::failure(ErrorCode::ProtocolError,
                               std::string("expected QUERY_RESPONSE but received ") +
                                   to_string(response.type));
      }
      const Status decoded = decode_query_response(response.payload.data(), response.payload.size(),
                                                   config.limits, out);
      if (!decoded.ok()) return decoded;
      if (out.code != ErrorCode::Ok) {
        return Status::failure(out.code, out.message);
      }
      return Status::success();
    }
  }
};

CapabilityClient::CapabilityClient(ClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

CapabilityClient::~CapabilityClient() {
  if (impl_ && impl_->connection.valid()) impl_->connection.close();
}

Status CapabilityClient::connect() {
  const Status connected = connect_loopback(impl_->config.port, impl_->connection);
  if (!connected.ok()) return connected;
  HelloMessage hello;
  hello.protocol_version = protocol_version();
  hello.agent = impl_->config.agent;
  hello.role = "client";
  std::vector<std::uint8_t> payload;
  Status encoded = encode_hello(hello, impl_->config.limits, payload);
  if (!encoded.ok()) return encoded;
  Frame frame;
  frame.type = MessageType::Hello;
  frame.correlation = ++impl_->correlation;
  frame.payload = std::move(payload);
  Status status = send_frame(impl_->connection, frame, impl_->config.limits);
  if (!status.ok()) return status;
  Frame response;
  status = receive_frame(impl_->connection, impl_->decoder, response);
  if (!status.ok()) return status;
  if (response.type != MessageType::Welcome) {
    return Status::failure(ErrorCode::ProtocolError,
                           std::string("expected WELCOME but received ") + to_string(response.type));
  }
  WelcomeMessage welcome;
  status = decode_welcome(response.payload.data(), response.payload.size(), impl_->config.limits, welcome);
  if (!status.ok()) return status;
  if (welcome.protocol_version != protocol_version()) {
    return Status::failure(ErrorCode::UnsupportedVersion,
                           "coordinator protocol version " + std::to_string(welcome.protocol_version) +
                               " is not supported");
  }
  impl_->epoch = welcome.epoch;
  return Status::success();
}

Status CapabilityClient::disconnect() { return impl_->connection.close(); }

bool CapabilityClient::connected() const noexcept { return impl_ && impl_->connection.valid(); }

CoordinatorEpoch CapabilityClient::epoch() const noexcept { return impl_->epoch; }

Status CapabilityClient::query_capability(const CapabilityQuery& query, ResolvedCapability& out) {
  QueryRequest request;
  request.kind = QueryKind::Capability;
  request.capability = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.capability);
  return Status::success();
}

Status CapabilityClient::query_capabilities(const DeviceCapabilityQuery& query,
                                            std::vector<ResolvedCapability>& out) {
  QueryRequest request;
  request.kind = QueryKind::DeviceCapabilities;
  request.device_capabilities = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.capabilities);
  return Status::success();
}

Status CapabilityClient::query_fleet(const FleetCapabilityQuery& query,
                                     std::vector<ResolvedCapability>& out) {
  QueryRequest request;
  request.kind = QueryKind::Fleet;
  request.fleet = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.capabilities);
  return Status::success();
}

Status CapabilityClient::query_stale(const StaleCapabilityQuery& query,
                                     std::vector<ResolvedCapability>& out) {
  QueryRequest request;
  request.kind = QueryKind::StaleCapabilities;
  request.stale = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.capabilities);
  return Status::success();
}

Status CapabilityClient::query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out) {
  QueryRequest request;
  request.kind = QueryKind::Quirks;
  request.quirks = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.quirks);
  return Status::success();
}

Status CapabilityClient::list_quirks(std::vector<QuirkRecord>& out) {
  QueryRequest request;
  request.kind = QueryKind::QuirkRecords;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.quirk_records);
  return Status::success();
}

Status CapabilityClient::query_compatibility(const CompatibilityQuery& query,
                                             std::vector<CompatibilityFact>& out) {
  QueryRequest request;
  request.kind = QueryKind::Compatibility;
  request.compatibility = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.compatibility);
  return Status::success();
}

Status CapabilityClient::compare(const ComparisonRequest& request, DeviceComparison& out) {
  QueryRequest query;
  query.kind = QueryKind::Compare;
  query.comparison = request;
  QueryResponse response;
  const Status status = impl_->exchange(query, response);
  if (!status.ok()) return status;
  out = std::move(response.comparison);
  return Status::success();
}

Status CapabilityClient::list_devices(std::vector<SubjectSnapshot>& out) {
  QueryRequest request;
  request.kind = QueryKind::Devices;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.subjects);
  return Status::success();
}

Status CapabilityClient::list_publishers(std::vector<PublisherRecord>& out) {
  QueryRequest request;
  request.kind = QueryKind::Publishers;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.publishers);
  return Status::success();
}

Status CapabilityClient::snapshot(RegistrySnapshot& out) {
  QueryRequest request;
  request.kind = QueryKind::Snapshot;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = std::move(response.snapshot);
  return Status::success();
}

Status CapabilityClient::explain(const CapabilityQuery& query, std::string& out) {
  QueryRequest request;
  request.kind = QueryKind::Explain;
  request.capability = query;
  QueryResponse response;
  const Status status = impl_->exchange(request, response);
  if (!status.ok()) return status;
  out = response.text.empty() ? response.capability.explanation : response.text;
  return Status::success();
}

Status CapabilityClient::fence_publisher(const PublisherId& publisher, std::string reason) {
  if (!impl_->connection.valid()) {
    return Status::failure(ErrorCode::IoFailure, "client is not connected");
  }
  FencePublisherMessage message;
  message.publisher = publisher;
  message.reason = std::move(reason);
  std::vector<std::uint8_t> payload;
  const Status encoded = encode_fence_publisher(message, impl_->config.limits, payload);
  if (!encoded.ok()) return encoded;
  Frame frame;
  frame.type = MessageType::FencePublisher;
  frame.correlation = ++impl_->correlation;
  frame.payload = std::move(payload);
  const Status sent = send_frame(impl_->connection, frame, impl_->config.limits);
  if (!sent.ok()) return sent;
  Frame response;
  const Status received = receive_frame(impl_->connection, impl_->decoder, response);
  if (!received.ok()) return received;
  if (response.type != MessageType::FenceAccepted) {
    return Status::failure(ErrorCode::ProtocolError, "coordinator did not confirm the fence");
  }
  FenceAcceptedMessage accepted;
  const Status decoded = decode_fence_accepted(response.payload.data(), response.payload.size(),
                                               impl_->config.limits, accepted);
  if (!decoded.ok()) return decoded;
  if (!apply_code_accepted(accepted.code)) {
    return Status::failure(ErrorCode::FencedPublisher,
                           std::string(to_string(accepted.code)) + ": " + accepted.detail);
  }
  return Status::success();
}

Status CapabilityClient::shutdown(std::string reason) {
  if (!impl_->connection.valid()) {
    return Status::failure(ErrorCode::IoFailure, "client is not connected");
  }
  ShutdownMessage message;
  message.reason = std::move(reason);
  std::vector<std::uint8_t> payload;
  const Status encoded = encode_shutdown(message, impl_->config.limits, payload);
  if (!encoded.ok()) return encoded;
  Frame frame;
  frame.type = MessageType::ShutdownRequest;
  frame.correlation = ++impl_->correlation;
  frame.payload = std::move(payload);
  const Status sent = send_frame(impl_->connection, frame, impl_->config.limits);
  if (!sent.ok()) return sent;
  Frame response;
  return receive_frame(impl_->connection, impl_->decoder, response);
}

}  // namespace hcr
