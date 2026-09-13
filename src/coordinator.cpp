// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/coordinator.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "hcr/protocol.hpp"
#include "hcr/render.hpp"
#include "hcr/transport.hpp"

namespace hcr {

void ShutdownToken::request(std::string reason) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (reason_.empty()) reason_ = std::move(reason);
  }
  requested_.store(true, std::memory_order_release);
}

std::string ShutdownToken::reason() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return reason_;
}

struct CapabilityCoordinator::Impl {
  explicit Impl(CoordinatorConfig coordinator_config)
      : config(std::move(coordinator_config)), registry(config.limits) {}

  CoordinatorConfig config;
  CapabilityRegistry registry;
  std::unique_ptr<StateStore> store;
  TcpListener listener;
  ShutdownToken token;

  struct Session {
    TcpConnection connection;
    FrameDecoder decoder;
    PublisherId publisher;
    PublisherBootId boot;
    bool publisher_registered = false;
    std::string adapter;
    bool handshake_complete = false;
    std::size_t delivered = 0;

    explicit Session(RegistryLimits limits) : decoder(std::move(limits)) {}
  };

  /// Event-loop statistics. Relaxed atomics are used instead of a lock so that
  /// the loop never holds an internal lock across socket I/O, and an observer
  /// thread can read a consistent snapshot without contending with it.
  struct Counters {
    std::atomic<std::uint64_t> connections_accepted{0};
    std::atomic<std::uint64_t> frames_received{0};
    std::atomic<std::uint64_t> frames_rejected{0};
    std::atomic<std::uint64_t> publications_applied{0};
    std::atomic<std::uint64_t> publications_rejected{0};
    std::atomic<std::uint64_t> publisher_deaths{0};
    std::atomic<std::uint64_t> publisher_fences{0};
    std::atomic<std::uint64_t> backpressure_events{0};
    std::atomic<std::uint64_t> sessions_fenced{0};
    std::atomic<std::uint64_t> queries_served{0};
    std::atomic<std::uint64_t> epoch_advances{0};

    [[nodiscard]] CoordinatorStats snapshot() const {
      CoordinatorStats out;
      out.connections_accepted = connections_accepted.load(std::memory_order_relaxed);
      out.frames_received = frames_received.load(std::memory_order_relaxed);
      out.frames_rejected = frames_rejected.load(std::memory_order_relaxed);
      out.publications_applied = publications_applied.load(std::memory_order_relaxed);
      out.publications_rejected = publications_rejected.load(std::memory_order_relaxed);
      out.publisher_deaths = publisher_deaths.load(std::memory_order_relaxed);
      out.publisher_fences = publisher_fences.load(std::memory_order_relaxed);
      out.backpressure_events = backpressure_events.load(std::memory_order_relaxed);
      out.sessions_fenced = sessions_fenced.load(std::memory_order_relaxed);
      out.queries_served = queries_served.load(std::memory_order_relaxed);
      out.epoch_advances = epoch_advances.load(std::memory_order_relaxed);
      return out;
    }
  };

  /// Guards the session map, which the event loop owns and which the destructor
  /// also touches. It is never held across socket or registry calls.
  mutable std::mutex mutex;
  Counters stats;
  std::map<std::uintptr_t, std::unique_ptr<Session>> sessions;
  bool started = false;

  Status respond(Session& session, MessageType type, std::uint64_t correlation,
                 const std::vector<std::uint8_t>& payload) {
    Frame frame;
    frame.type = type;
    frame.flags = kFrameFlagAck;
    frame.correlation = correlation;
    frame.payload = payload;
    return send_frame(session.connection, frame, config.limits);
  }

  Status send_error(Session& session, std::uint64_t correlation, ErrorCode code, const std::string& message) {
    ErrorMessage payload_message;
    payload_message.code = code;
    payload_message.message = message;
    std::vector<std::uint8_t> payload;
    const Status encoded = encode_error_message(payload_message, config.limits, payload);
    if (!encoded.ok()) return encoded;
    Frame frame;
    frame.type = MessageType::ErrorResponse;
    frame.correlation = correlation;
    frame.payload = std::move(payload);
    return send_frame(session.connection, frame, config.limits);
  }

  /// Interprets one frame. Called by the event-loop thread only, which owns
  /// the session map; no internal lock is held while frames are interpreted or
  /// responses are written, so the event loop never blocks an observer.
  Status dispatch(Session& session, const Frame& frame) {
    ++stats.frames_received;
    switch (frame.type) {
      case MessageType::Hello: {
        HelloMessage hello;
        const Status decoded = decode_hello(frame.payload.data(), frame.payload.size(), config.limits, hello);
        if (!decoded.ok()) return reject(session, frame, decoded);
        if (hello.protocol_version != protocol_version()) {
          return reject(session, frame,
                        Status::failure(ErrorCode::UnsupportedVersion,
                                        "client protocol version " + std::to_string(hello.protocol_version) +
                                            " is not supported"));
        }
        session.handshake_complete = true;
        WelcomeMessage welcome;
        welcome.protocol_version = protocol_version();
        welcome.epoch = registry.coordinator_epoch();
        welcome.coordinator_name = config.node_name;
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_welcome(welcome, config.limits, payload);
        if (!encoded.ok()) return encoded;
        return respond(session, MessageType::Welcome, frame.correlation, payload);
      }
      case MessageType::RegisterPublisher: {
        if (!session.handshake_complete) {
          return reject(session, frame, Status::failure(ErrorCode::ProtocolError, "HELLO is required first"));
        }
        RegisterPublisherMessage request;
        const Status decoded =
            decode_register_publisher(frame.payload.data(), frame.payload.size(), config.limits, request);
        if (!decoded.ok()) return reject(session, frame, decoded);
        const ApplyResult applied = registry.register_publisher(request.identity, request.adapter);
        PublisherAcceptedMessage accepted;
        accepted.code = applied.code;
        accepted.epoch = registry.coordinator_epoch();
        accepted.detail = applied.detail;
        accepted.identity = applied.assigned_publisher;
        if (applied.accepted()) {
          session.publisher = accepted.identity.id;
          session.boot = accepted.identity.boot;
          session.adapter = request.adapter;
          session.publisher_registered = true;
        }
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_publisher_accepted(accepted, config.limits, payload);
        if (!encoded.ok()) return encoded;
        return respond(session, MessageType::PublisherAccepted, frame.correlation, payload);
      }
      case MessageType::Publication: {
        Publication publication;
        const Status decoded =
            decode_publication(frame.payload.data(), frame.payload.size(), config.limits, publication);
        if (!decoded.ok()) return reject(session, frame, decoded);
        const ApplyResult applied = registry.apply(publication);
        if (applied.accepted()) {
          ++stats.publications_applied;
        } else {
          ++stats.publications_rejected;
        }
        PublicationAcceptedMessage accepted;
        accepted.code = applied.code;
        accepted.detail = applied.detail;
        accepted.device_generation = applied.device_generation;
        accepted.device_boot = applied.device_boot;
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_publication_accepted(accepted, config.limits, payload);
        if (!encoded.ok()) return encoded;
        return respond(session, MessageType::PublicationAccepted, frame.correlation, payload);
      }
      case MessageType::Heartbeat: {
        HeartbeatMessage heartbeat;
        const Status decoded =
            decode_heartbeat(frame.payload.data(), frame.payload.size(), config.limits, heartbeat);
        if (!decoded.ok()) return reject(session, frame, decoded);
        HeartbeatMessage reply;
        reply.sequence = heartbeat.sequence;
        reply.identity = heartbeat.identity;
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_heartbeat(reply, config.limits, payload);
        if (!encoded.ok()) return encoded;
        return respond(session, MessageType::HeartbeatAccepted, frame.correlation, payload);
      }
      case MessageType::FencePublisher: {
        FencePublisherMessage request;
        const Status decoded =
            decode_fence_publisher(frame.payload.data(), frame.payload.size(), config.limits, request);
        if (!decoded.ok()) return reject(session, frame, decoded);
        const ApplyResult applied = registry.fence_publisher(request.publisher, request.reason);
        if (applied.accepted()) ++stats.publisher_fences;
        FenceAcceptedMessage accepted;
        accepted.code = applied.code;
        accepted.detail = applied.detail;
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_fence_accepted(accepted, config.limits, payload);
        if (!encoded.ok()) return encoded;
        return respond(session, MessageType::FenceAccepted, frame.correlation, payload);
      }
      case MessageType::QueryRequest: {
        QueryRequest request;
        const Status decoded =
            decode_query_request(frame.payload.data(), frame.payload.size(), config.limits, request);
        if (!decoded.ok()) return reject(session, frame, decoded);
        return serve_query(session, frame, request);
      }
      case MessageType::SnapshotRequest: {
        QueryRequest request;
        request.kind = QueryKind::Snapshot;
        return serve_query(session, frame, request);
      }
      case MessageType::ShutdownRequest: {
        ShutdownMessage request;
        const Status decoded =
            decode_shutdown(frame.payload.data(), frame.payload.size(), config.limits, request);
        if (!decoded.ok()) return reject(session, frame, decoded);
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_shutdown(request, config.limits, payload);
        if (!encoded.ok()) return encoded;
        const Status sent = respond(session, MessageType::ShutdownAccepted, frame.correlation, payload);
        token.request(request.reason.empty() ? "shutdown requested by client" : request.reason);
        return sent;
      }
      default:
        return reject(session, frame,
                      Status::failure(ErrorCode::ProtocolError,
                                      std::string("message type ") + to_string(frame.type) +
                                          " is not valid for a coordinator"));
    }
  }

  Status reject(Session& session, const Frame& frame, const Status& status) {
    ++stats.frames_rejected;
    return send_error(session, frame.correlation, status.code(), status.message());
  }

  Status serve_query(Session& session, const Frame& frame, const QueryRequest& request) {
    ++stats.queries_served;
    QueryResponse response;
    response.kind = request.kind;
    Status status = Status::success();
    switch (request.kind) {
      case QueryKind::Capability:
      case QueryKind::Explain: {
        ResolvedCapability resolved;
        status = registry.query_capability(request.capability, resolved);
        response.capability = std::move(resolved);
        if (request.kind == QueryKind::Explain) response.text = response.capability.explanation;
        break;
      }
      case QueryKind::DeviceCapabilities:
        status = registry.query_capabilities(request.device_capabilities, response.capabilities);
        break;
      case QueryKind::Fleet:
        status = registry.query_fleet(request.fleet, response.capabilities);
        break;
      case QueryKind::StaleCapabilities:
        status = registry.query_stale(request.stale, response.capabilities);
        break;
      case QueryKind::Quirks:
        status = registry.query_quirks(request.quirks, response.quirks);
        break;
      case QueryKind::QuirkRecords:
        status = registry.list_quirks(response.quirk_records);
        break;
      case QueryKind::Compatibility:
        status = registry.query_compatibility(request.compatibility, response.compatibility);
        break;
      case QueryKind::Compare:
        status = registry.compare_devices(request.comparison, response.comparison);
        break;
      case QueryKind::Devices:
        status = registry.list_devices(response.subjects);
        break;
      case QueryKind::Publishers: {
        std::vector<PublisherRecord> publishers;
        status = registry.list_publishers(request.publishers, publishers);
        response.publishers = std::move(publishers);
        break;
      }
      case QueryKind::Snapshot:
        status = registry.snapshot(response.snapshot);
        break;
    }
    if (!status.ok()) {
      response.code = status.code();
      response.message = status.message();
    }
    std::vector<std::uint8_t> payload;
    const Status encoded = encode_query_response(response, config.limits, payload);
    if (!encoded.ok()) {
      return reject(session, frame, encoded);
    }
    if (payload.size() > config.limits.max_frame_bytes) {
      ErrorMessage error;
      error.code = ErrorCode::LimitExceeded;
      error.message = "query response exceeds the frame bound";
      std::vector<std::uint8_t> error_payload;
      const Status error_encoded = encode_error_message(error, config.limits, error_payload);
      if (!error_encoded.ok()) return error_encoded;
      return respond(session, MessageType::ErrorResponse, frame.correlation, error_payload);
    }
    return respond(session, MessageType::QueryResponse, frame.correlation, payload);
  }

  Session* find_session(std::uintptr_t handle) {
    std::lock_guard<std::mutex> guard(mutex);
    const auto it = sessions.find(handle);
    return it == sessions.end() ? nullptr : it->second.get();
  }

  void drop_session(std::uintptr_t handle, const std::string& reason) {
    std::lock_guard<std::mutex> guard(mutex);
    const auto it = sessions.find(handle);
    if (it == sessions.end()) return;
    retire_session(*it->second, reason);
    ++stats.sessions_fenced;
    sessions.erase(it);
  }

  void retire_session(Session& session, const std::string& reason) {
    if (session.publisher_registered) {
      const ApplyResult retired = registry.retire_publisher_boot(session.publisher, session.boot, reason);
      if (retired.accepted() || retired.code == ApplyCode::AcceptedIdempotent) {
        ++stats.publisher_deaths;
      }
    }
    session.connection.close();
  }

  /// Phase one of the event loop: read whatever a ready session has delivered
  /// and collect complete frames. Death (orderly close or socket error) is
  /// reported so that the caller can retire the session before any frame from
  /// any session is interpreted.
  Status read_session(Session& session, std::vector<Frame>& frames, bool& closed) {
    closed = false;
    frames.clear();
    std::size_t pending = 0;
    const Status available = session.connection.available(pending);
    if (!available.ok()) {
      closed = true;
      return Status::success();
    }
    std::vector<std::uint8_t> buffer(pending == 0 ? 1 : pending);
    std::size_t received = 0;
    const Status read = session.connection.recv_some(buffer.data(), buffer.size(), received);
    if (!read.ok()) {
      closed = true;
      return Status::success();
    }
    if (received == 0) {
      closed = true;
      return Status::success();
    }
    const Status fed = session.decoder.feed(buffer.data(), received, frames);
    if (!fed.ok()) {
      ++stats.frames_rejected;
      const Status notified = send_error(session, 0, fed.code(), fed.message());
      (void)notified;
      closed = true;
      frames.clear();
      return Status::success();
    }
    if (frames.size() > config.limits.max_pending_publications) {
      ++stats.backpressure_events;
      ++stats.frames_rejected;
      const Status notified =
          send_error(session, 0, ErrorCode::QueueFull, "too many frames delivered in one batch");
      (void)notified;
      closed = true;
      frames.clear();
      return Status::success();
    }
    return Status::success();
  }

  /// Phase two: interpret the collected frames.
  Status dispatch_frames(Session& session, const std::vector<Frame>& frames, bool& closed) {
    closed = false;
    for (const Frame& frame : frames) {
      const Status dispatched = dispatch(session, frame);
      if (!dispatched.ok()) {
        closed = true;
        return Status::success();
      }
      session.delivered += 1;
      if (token.requested()) break;
    }
    return Status::success();
  }
};

CapabilityCoordinator::CapabilityCoordinator(CoordinatorConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CapabilityCoordinator::~CapabilityCoordinator() {
  if (impl_) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    for (auto& entry : impl_->sessions) {
      entry.second->connection.close();
    }
    impl_->sessions.clear();
    impl_->listener.close();
  }
}

Status CapabilityCoordinator::start() {
  const Status initialized = transport_initialize();
  if (!initialized.ok()) return initialized;
  const Status opened = impl_->listener.open_loopback(impl_->config.port, impl_->config.limits.max_connections);
  if (!opened.ok()) return opened;
  if (!impl_->config.state_path.empty()) {
    impl_->store = std::make_unique<FileStateStore>(impl_->config.state_path);
    if (impl_->store->exists()) {
      // An existing image must load cleanly; a missing one simply means this
      // coordinator has no prior state to restore.
      const Status loaded = load_state();
      if (!loaded.ok()) return loaded;
    }
  }
  CoordinatorEpoch epoch;
  const Status advanced = impl_->registry.advance_coordinator_epoch(epoch);
  if (!advanced.ok()) return advanced;
  ++impl_->stats.epoch_advances;
  impl_->registry.reset_publisher_liveness("coordinator restarted; publisher liveness is not restored");
  impl_->started = true;
  if (!impl_->config.state_path.empty() && impl_->config.persist_on_mutation) {
    return save_state();
  }
  return Status::success();
}

std::uint16_t CapabilityCoordinator::port() const noexcept {
  std::uint16_t value = 0;
  impl_->listener.local_port(value);
  return value;
}

CoordinatorEpoch CapabilityCoordinator::epoch() const { return impl_->registry.coordinator_epoch(); }

CapabilityRegistry& CapabilityCoordinator::registry() noexcept { return impl_->registry; }

CoordinatorStats CapabilityCoordinator::stats() const { return impl_->stats.snapshot(); }

Status CapabilityCoordinator::request_shutdown(std::string reason) {
  impl_->token.request(std::move(reason));
  return Status::success();
}

Status CapabilityCoordinator::load_state() {
  if (!impl_->store) return Status::success();
  DurableState state;
  const Status loaded = impl_->store->load(state);
  if (!loaded.ok()) return loaded;
  ImportReport report;
  const Status imported = impl_->registry.import_durable_state(state, report);
  if (!imported.ok()) return imported;
  return Status::success();
}

Status CapabilityCoordinator::save_state() {
  if (!impl_->store) return Status::success();
  const DurableState state = impl_->registry.export_durable_state();
  return impl_->store->save(state);
}

Status CapabilityCoordinator::run(ShutdownToken* external_token) {
  if (!impl_->started) {
    const Status started = start();
    if (!started.ok()) return started;
  }
  while (true) {
    if (impl_->token.requested()) break;
    if (external_token != nullptr && external_token->requested()) {
      impl_->token.request(external_token->reason());
      break;
    }
    std::vector<std::uintptr_t> handles;
    {
      std::lock_guard<std::mutex> guard(impl_->mutex);
      for (const auto& entry : impl_->sessions) handles.push_back(entry.first);
    }
    // Readiness multiplexing blocks until activity arrives and never applies a
    // timeout. A hang is a defect, not something to paper over.
    bool listener_ready = false;
    std::vector<bool> ready;
    const Status waited = wait_all_readable(impl_->listener.native_handle(), handles, listener_ready, ready);
    if (!waited.ok()) return waited;
    if (listener_ready) {
      TcpConnection accepted;
      const Status status = impl_->listener.accept(accepted);
      if (!status.ok()) return status;
      std::lock_guard<std::mutex> guard(impl_->mutex);
      if (impl_->sessions.size() >= impl_->config.limits.max_connections) {
        ++impl_->stats.backpressure_events;
        ErrorMessage error;
        error.code = ErrorCode::QueueFull;
        error.message = "connection limit reached";
        std::vector<std::uint8_t> payload;
        const Status encoded = encode_error_message(error, impl_->config.limits, payload);
        if (encoded.ok()) {
          Frame frame;
          frame.type = MessageType::ErrorResponse;
          frame.payload = std::move(payload);
          const Status sent = send_frame(accepted, frame, impl_->config.limits);
          (void)sent;
        }
        accepted.close();
      } else {
        const std::uintptr_t handle = accepted.native_handle();
        auto session = std::make_unique<Impl::Session>(impl_->config.limits);
        session->connection = std::move(accepted);
        impl_->sessions.emplace(handle, std::move(session));
        ++impl_->stats.connections_accepted;
      }
    }
    // Death is observed before evidence is interpreted: every ready session is
    // read first, dead sessions are retired, and only then are frames
    // dispatched. The logical outcome therefore does not depend on the order in
    // which the kernel reports readiness.
    std::vector<std::uintptr_t> dead;
    std::vector<std::pair<std::uintptr_t, std::vector<Frame>>> delivered;
    for (std::size_t index = 0; index < handles.size(); ++index) {
      if (!ready[index]) continue;
      Impl::Session* session = impl_->find_session(handles[index]);
      if (session == nullptr) continue;
      std::vector<Frame> frames;
      bool closed = false;
      const Status read = impl_->read_session(*session, frames, closed);
      if (!read.ok()) return read;
      if (closed) {
        dead.push_back(handles[index]);
      } else if (!frames.empty()) {
        delivered.emplace_back(handles[index], std::move(frames));
      }
    }
    for (const std::uintptr_t handle : dead) {
      impl_->drop_session(handle, "publisher connection closed; boot identity retired");
    }
    for (const auto& entry : delivered) {
      Impl::Session* session = impl_->find_session(entry.first);
      if (session == nullptr) continue;
      bool closed = false;
      const Status dispatched = impl_->dispatch_frames(*session, entry.second, closed);
      if (!dispatched.ok()) return dispatched;
      if (closed) {
        impl_->drop_session(entry.first, "session closed during dispatch");
      }
    }
  }

  // Ordered shutdown: stop accepting publishers, fence every remaining
  // session, persist committed state, then release the listener.
  impl_->listener.close();
  {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    for (auto& entry : impl_->sessions) {
      impl_->retire_session(*entry.second, "coordinator shutdown; boot identity retired");
      ++impl_->stats.sessions_fenced;
    }
    impl_->sessions.clear();
  }
  const Status shutting_down = impl_->registry.begin_shutdown();
  if (!shutting_down.ok()) return shutting_down;
  if (impl_->store) {
    const Status saved = save_state();
    if (!saved.ok()) return saved;
  }
  return Status::success();
}

}  // namespace hcr
