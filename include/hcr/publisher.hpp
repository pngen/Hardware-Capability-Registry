// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "hcr/discovery.hpp"
#include "hcr/protocol.hpp"
#include "hcr/transport.hpp"

namespace hcr {

struct PublisherConfig {
  std::uint16_t port = 0;
  PublisherId publisher;
  std::string adapter;
  std::string agent = "hcr-publisher";
  std::uint64_t nonce = 0;
  RegistryLimits limits;
};

/// Remote evidence publisher.
///
/// A publisher holds authority only for its own boot identity and only for the
/// coordinator epoch that admitted it. Sequence and evidence generations are
/// issued here, so a restarting publisher necessarily presents a fresh boot
/// identity and fresh evidence.
class CapabilityPublisher final : public EvidenceSink {
 public:
  explicit CapabilityPublisher(PublisherConfig config);
  ~CapabilityPublisher() override;

  CapabilityPublisher(const CapabilityPublisher&) = delete;
  CapabilityPublisher& operator=(const CapabilityPublisher&) = delete;

  /// Connects, performs HELLO/WELCOME, then REGISTER_PUBLISHER. The publisher
  /// boot identity is assigned by the coordinator, so a restarted publisher
  /// cannot reuse the authority of a dead one.
  Status connect_and_register();

  ApplyResult ensure_publisher(const PublisherIdentity& identity, std::string adapter) override;
  std::uint64_t next_sequence(const PublisherId& publisher) override;
  ApplyResult publish(const Publication& publication) override;

  /// Sends a heartbeat and waits for the coordinator's acknowledgement. Used by
  /// long-lived publishers to prove liveness; a failure means the coordinator
  /// session is gone.
  ApplyResult heartbeat();

  Status disconnect();
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] PublisherIdentity identity() const;
  [[nodiscard]] CoordinatorEpoch epoch() const noexcept;
  [[nodiscard]] std::uint64_t next_sequence() noexcept;
  [[nodiscard]] EvidenceGeneration next_evidence_generation() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hcr
