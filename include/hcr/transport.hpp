// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hcr/status.hpp"

namespace hcr {

/// Blocking TCP transport. The registry deliberately uses real OS sockets so
/// that publisher and coordinator failure proofs are real process failures.
/// No timeouts are configured anywhere: a hang is a defect, not something the
/// transport papers over.
class TcpConnection {
 public:
  TcpConnection() = default;
  ~TcpConnection();
  TcpConnection(TcpConnection&& other) noexcept;
  TcpConnection& operator=(TcpConnection&& other) noexcept;
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  Status send_all(const std::uint8_t* data, std::size_t size);
  /// Returns 0 in received on orderly shutdown by the peer.
  Status recv_some(std::uint8_t* data, std::size_t capacity, std::size_t& received);
  /// Bytes currently readable without blocking.
  Status available(std::size_t& bytes) const;
  Status close();
  [[nodiscard]] std::string peer() const;
  /// Native socket handle, exposed for readiness multiplexing only.
  [[nodiscard]] std::uintptr_t native_handle() const noexcept { return handle_; }

  /// Adopts an already-connected native socket. Ownership transfers to the
  /// connection, which closes it.
  explicit TcpConnection(std::uintptr_t handle) : handle_(handle) {}

 private:
  std::uintptr_t handle_ = kInvalidHandle;
  static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(~0ull);
};

class TcpListener {
 public:
  TcpListener() = default;
  ~TcpListener();
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;
  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  /// Binds to the loopback interface. Port 0 selects an ephemeral port; the
  /// chosen port is reported by local_port().
  Status open_loopback(std::uint16_t port, std::size_t backlog);
  Status local_port(std::uint16_t& out) const;
  [[nodiscard]] bool valid() const noexcept;
  Status accept(TcpConnection& out);
  Status close();
  /// Native listener handle, exposed for readiness multiplexing only.
  [[nodiscard]] std::uintptr_t native_handle() const noexcept { return handle_; }

 private:
  std::uintptr_t handle_ = kInvalidHandle;
  static constexpr std::uintptr_t kInvalidHandle = static_cast<std::uintptr_t>(~0ull);
};

Status connect_loopback(std::uint16_t port, TcpConnection& out);

/// Waits until at least one of the two sockets is readable. A null second
/// socket waits on the first alone. Returns the ready flags in the same order.
Status wait_readable(const TcpConnection* first, const TcpConnection* second, bool& first_ready, bool& second_ready);

/// Waits until the listener or at least one listed socket is readable. Blocks
/// indefinitely: the registry configures no timeouts anywhere. Raw handles are
/// used so the caller keeps ownership of every socket.
Status wait_all_readable(std::uintptr_t listener,
                         const std::vector<std::uintptr_t>& sockets,
                         bool& listener_ready,
                         std::vector<bool>& ready);

/// Process-wide transport initialization. Called automatically on first use.
Status transport_initialize();

}  // namespace hcr
