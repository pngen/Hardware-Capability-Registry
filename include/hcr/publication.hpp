// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/provenance.hpp"
#include "hcr/quirk.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Publication kinds accepted by the registry. Everything that can change
/// registry truth travels through this single ingestion path, whether it
/// arrives in-process from a discovery adapter or over the wire from a remote
/// publisher.
enum class PublicationKind {
  RegisterPublisher = 0,
  RegisterDevice,
  PublishDeviceGeneration,
  PublishCapability,
  WithdrawCapability,
  PublishQuirk,
  WithdrawQuirk,
  PublishCompatibility,
  WithdrawCompatibility,
  RegisterCapabilitySchema,
  Heartbeat,
};

const char* to_string(PublicationKind kind) noexcept;
bool publication_kind_from_string(std::string_view text, PublicationKind& out) noexcept;

/// A single unit of evidence or structural change.
struct Publication {
  PublicationKind kind = PublicationKind::RegisterDevice;
  PublisherIdentity publisher;
  /// Strictly increasing per publisher boot. Replayed or out-of-order
  /// sequences are rejected before any mutation.
  std::uint64_t sequence = 0;
  /// Monotonic per publisher boot.
  EvidenceGeneration evidence_generation;
  DeviceRegistration device;
  CapabilityRecord capability;
  QuirkRecord quirk;
  CompatibilityFact compatibility;
  CapabilitySchema schema;
  std::string reason;
  std::string issued_at_utc;
};

Status validate_publication(const Publication& publication, const RegistryLimits& limits);

/// Result code of an ingestion attempt. Rejections never mutate state.
enum class ApplyCode {
  Accepted = 0,
  AcceptedIdempotent,
  RejectedValidation,
  RejectedUnknownPublisher,
  RejectedFencedPublisher,
  RejectedStaleBoot,
  RejectedStaleEpoch,
  RejectedStaleSequence,
  RejectedGenerationRegression,
  RejectedDuplicateConflict,
  RejectedStaleDeviceGeneration,
  RejectedStaleCapabilityGeneration,
  RejectedUnknownDevice,
  RejectedUnknownSchema,
  RejectedLimit,
  RejectedBackpressure,
  RejectedShuttingDown,
  RejectedWithdrawnSubject,
  RejectedInvariant,
};

const char* to_string(ApplyCode code) noexcept;
bool apply_code_accepted(ApplyCode code) noexcept;

struct ApplyResult {
  ApplyCode code = ApplyCode::RejectedValidation;
  bool mutated = false;
  std::string detail;
  /// Identity assigned by the registry when a publisher registers with boot 0.
  /// Publishers adopt this identity for every subsequent publication, so a
  /// restarted publisher can never reuse the authority of a dead boot.
  PublisherIdentity assigned_publisher;
  /// Generations assigned by the registry to a device registration. Adapters
  /// bind subsequent capability evidence to these values; they are never
  /// chosen by a caller.
  DeviceGeneration device_generation;
  DeviceBootId device_boot;

  [[nodiscard]] bool accepted() const noexcept { return apply_code_accepted(code); }
};

}  // namespace hcr
