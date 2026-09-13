// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/hardware.hpp"
#include "hcr/ids.hpp"
#include "hcr/provenance.hpp"
#include "hcr/quirk.hpp"
#include "hcr/snapshot.hpp"

namespace hcr {

/// Persisted header. Magic, format version, epoch and revision are validated
/// before any record is interpreted.
struct DurableStateHeader {
  std::uint32_t format_version = 0;
  CoordinatorEpoch epoch;
  SnapshotGeneration snapshot_generation;
  std::uint64_t state_revision = 0;
  std::string created_at_utc;
  std::string producer;
};

/// Durable replay watermark for one publisher boot.
///
/// Watermarks persist across restarts so that replayed frames from an old boot
/// identity are rejected forever, even after the coordinator is replaced.
struct PublisherWatermark {
  PublisherId publisher;
  PublisherBootId boot;
  std::uint64_t max_sequence = 0;
  EvidenceGeneration max_evidence_generation;
  PublisherState state = PublisherState::Unknown;
  std::string adapter;
  std::string reason;
  std::size_t accepted = 0;
  std::size_t rejected = 0;
  std::string registered_at_utc;
};

/// Semantically durable registry state.
///
/// Everything here survives a restart. What does *not* survive is process
/// liveness: after a load, publishers are unregistered and process-bound
/// evidence is marked REVALIDATION_REQUIRED rather than restored as current.
struct DurableState {
  DurableStateHeader header;
  std::vector<PlatformIdentity> platforms;
  std::vector<SubjectSnapshot> subjects;
  std::vector<CapabilitySchema> schemas;
  /// Complete retained evidence history, immutable once written.
  std::vector<CapabilityRecord> capability_history;
  std::vector<QuirkRecord> quirks;
  std::vector<CompatibilityFact> compatibility;
  std::vector<PublisherWatermark> publisher_watermarks;
  /// Fenced publisher boots. A fenced boot identity never regains authority.
  std::vector<PublisherWatermark> fenced_boots;
  std::uint64_t canonical_digest = 0;
};

/// Report produced by an import. A failed import applies nothing.
struct ImportReport {
  bool applied = false;
  std::size_t subjects = 0;
  std::size_t schemas = 0;
  std::size_t capability_records = 0;
  std::size_t revalidated_records = 0;
  std::size_t quirks = 0;
  std::size_t compatibility_facts = 0;
  std::size_t publishers = 0;
  std::size_t fenced_boots = 0;
  std::string detail;
};

}  // namespace hcr
