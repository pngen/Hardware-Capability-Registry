// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "hcr/persistence.hpp"
#include "hcr/registry.hpp"
#include "hcr/status.hpp"

namespace hcr {

struct CoordinatorConfig {
  std::uint16_t port = 0;  ///< 0 selects an ephemeral port
  std::string state_path;  ///< empty disables persistence
  RegistryLimits limits;
  bool persist_on_mutation = false;
  std::string node_name = "hcr-coordinator";
};

struct CoordinatorStats {
  std::uint64_t connections_accepted = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_rejected = 0;
  std::uint64_t publications_applied = 0;
  std::uint64_t publications_rejected = 0;
  std::uint64_t publisher_deaths = 0;
  std::uint64_t publisher_fences = 0;
  std::uint64_t backpressure_events = 0;
  std::uint64_t sessions_fenced = 0;
  std::uint64_t queries_served = 0;
  std::uint64_t epoch_advances = 0;
};

/// Cooperative shutdown flag. The coordinator's event loop polls it between
/// select() wakeups; any thread may request shutdown.
class ShutdownToken {
 public:
  void request(std::string reason);
  [[nodiscard]] bool requested() const noexcept { return requested_.load(std::memory_order_acquire); }
  [[nodiscard]] std::string reason() const;

 private:
  std::atomic<bool> requested_{false};
  mutable std::mutex mutex_;
  std::string reason_;
};

/// Distributed capability coordinator.
///
/// Owns publisher authority, coordinator epoch, canonical knowledge and
/// durable state. Publisher liveness is never inferred from durable state:
/// after a restart every publisher must re-register with a fresh boot identity
/// and republish live evidence before it becomes current again.
class CapabilityCoordinator {
 public:
  explicit CapabilityCoordinator(CoordinatorConfig config);
  ~CapabilityCoordinator();

  CapabilityCoordinator(const CapabilityCoordinator&) = delete;
  CapabilityCoordinator& operator=(const CapabilityCoordinator&) = delete;

  /// Binds the listener, loads durable state when configured, advances the
  /// coordinator epoch and revalidates process-bound evidence.
  Status start();

  /// Serves until shutdown is requested through the control channel or through
  /// the supplied token. Returns after all sessions have been fenced, sockets
  /// closed and committed state persisted.
  Status run(ShutdownToken* external_token);

  Status request_shutdown(std::string reason);

  [[nodiscard]] std::uint16_t port() const noexcept;
  [[nodiscard]] CoordinatorEpoch epoch() const;
  [[nodiscard]] CoordinatorStats stats() const;
  [[nodiscard]] CapabilityRegistry& registry() noexcept;

  /// Persists committed state. Never called while holding the registry lock.
  Status save_state();
  Status load_state();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hcr
