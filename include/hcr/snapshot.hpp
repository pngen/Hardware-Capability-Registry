// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hcr/compatibility.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/provenance.hpp"
#include "hcr/quirk.hpp"
#include "hcr/resolution.hpp"

namespace hcr {

/// Publisher metadata as recorded in a snapshot. Liveness is deliberately not
/// restored from durable state; a snapshot records authority, not process
/// liveness.
struct PublisherRecord {
  PublisherIdentity identity;
  PublisherState state = PublisherState::Unknown;
  std::string adapter;
  std::uint64_t last_sequence = 0;
  EvidenceGeneration max_evidence_generation;
  std::size_t accepted = 0;
  std::size_t rejected = 0;
  std::string reason;
  std::string registered_at_utc;
  std::size_t boot_history = 0;
};

/// One hardware subject as recorded in a snapshot.
struct SubjectSnapshot {
  DeviceIdentity identity;
  DeviceSoftwareState software;
  DeviceGeneration generation;
  DeviceBootId boot;
  PlatformGeneration platform_generation;
  TruthClass classification = TruthClass::Unknown;
  std::size_t capability_count = 0;
  std::size_t current_count = 0;
  std::size_t unknown_count = 0;
  std::size_t unsupported_count = 0;
  std::size_t stale_count = 0;
  std::size_t applicable_quirk_count = 0;
};

/// Immutable registry snapshot. A snapshot is a value: once produced it cannot
/// observe later mutation, and it carries the generation and epoch needed to
/// detect that it has become stale.
struct RegistrySnapshot {
  SnapshotGeneration generation;
  CoordinatorEpoch epoch;
  std::uint64_t state_revision = 0;
  std::string created_at_utc;
  std::vector<SubjectSnapshot> subjects;
  std::vector<ResolvedCapability> capabilities;
  std::vector<QuirkRecord> quirks;
  std::vector<CompatibilityFact> compatibility;
  std::vector<PublisherRecord> publishers;
  std::size_t real_statements = 0;
  std::size_t synthetic_statements = 0;
  std::size_t unsupported_statements = 0;
  std::size_t unknown_statements = 0;
  std::size_t stale_evidence_records = 0;
  std::uint64_t canonical_digest = 0;

  [[nodiscard]] std::size_t capability_count() const noexcept { return capabilities.size(); }
};

/// Detects whether a snapshot is stale relative to a newer snapshot of the
/// same registry. Sets the exact staleness flags.
StalenessFlag detect_snapshot_staleness(const RegistrySnapshot& snapshot, const RegistrySnapshot& current);

/// True when the snapshot still describes the registry's current state.
bool snapshot_is_current(const RegistrySnapshot& snapshot, const RegistrySnapshot& current);

}  // namespace hcr
