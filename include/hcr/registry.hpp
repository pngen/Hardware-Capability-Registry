// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/durable_state.hpp"
#include "hcr/hardware.hpp"
#include "hcr/limits.hpp"
#include "hcr/publication.hpp"
#include "hcr/query.hpp"
#include "hcr/resolution.hpp"
#include "hcr/snapshot.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Resource accounting exposed for observability and for tests that assert
/// bounds hold.
struct ResourceUsage {
  std::size_t devices = 0;
  std::size_t platforms = 0;
  std::size_t capability_keys = 0;
  std::size_t evidence_records = 0;
  std::size_t quirks = 0;
  std::size_t compatibility_facts = 0;
  std::size_t publishers = 0;
  std::size_t schemas = 0;
  std::uint64_t accepted_publications = 0;
  std::uint64_t rejected_publications = 0;
  std::uint64_t idempotent_publications = 0;
  std::uint64_t snapshots_taken = 0;
};

/// The canonical hardware capability knowledge boundary.
///
/// Ownership: canonical hardware identity, capability truth, evidence
/// provenance, quirk and compatibility knowledge, generations and snapshots.
/// Not owned: scheduling, placement, routing, allocation, health policy,
/// driver/firmware management, or any control-plane mutation of hardware.
///
/// Concurrency: all public methods are safe to call concurrently. Internal
/// state is guarded by a single writer-preferring mutex; no lock is ever held
/// across vendor API calls, device probes, socket I/O, filesystem I/O or
/// callbacks. Discovery adapters gather evidence outside the registry and
/// submit immutable publications through apply().
class CapabilityRegistry {
 public:
  explicit CapabilityRegistry(RegistryLimits limits = RegistryLimits{});
  ~CapabilityRegistry();

  CapabilityRegistry(const CapabilityRegistry&) = delete;
  CapabilityRegistry& operator=(const CapabilityRegistry&) = delete;
  CapabilityRegistry(CapabilityRegistry&&) = delete;
  CapabilityRegistry& operator=(CapabilityRegistry&&) = delete;

  [[nodiscard]] const RegistryLimits& limits() const noexcept;

  // -- capability schemas -------------------------------------------------
  Status register_capability_schema(const CapabilitySchema& schema);
  [[nodiscard]] bool has_capability_schema(const CapabilitySchemaId& schema) const;
  Status find_capability_schema(const CapabilitySchemaId& schema, CapabilitySchema& out) const;
  std::vector<CapabilitySchema> capability_schemas() const;

  // -- publisher authority ------------------------------------------------
  ApplyResult register_publisher(const PublisherIdentity& identity, std::string adapter);
  ApplyResult fence_publisher(const PublisherId& publisher, std::string reason);
  /// Permanently retires one boot identity of a live publisher. Used when a
  /// publisher process dies: the boot identity never regains authority, while
  /// the publisher identifier itself may return with a fresh boot.
  ApplyResult retire_publisher_boot(const PublisherId& publisher, PublisherBootId boot, std::string reason);
  Status find_publisher_record(const PublisherId& publisher, PublisherRecord& out) const;
  Status list_publishers(const PublisherQuery& query, std::vector<PublisherRecord>& out) const;
  /// Marks every publisher unregistered and revalidates process-bound evidence.
  /// Called after a restart before any publisher is admitted again.
  Status reset_publisher_liveness(std::string reason);

  // -- ingestion ----------------------------------------------------------
  ApplyResult apply(const Publication& publication);

  // -- subjects -----------------------------------------------------------
  Status find_device(const DeviceId& device, SubjectSnapshot& out) const;
  Status find_platform(const PlatformId& platform, PlatformIdentity& out) const;
  Status list_devices(std::vector<SubjectSnapshot>& out) const;
  [[nodiscard]] bool has_device(const DeviceId& device) const;

  // -- queries ------------------------------------------------------------
  Status query_capability(const CapabilityQuery& query, ResolvedCapability& out) const;
  Status query_capabilities(const DeviceCapabilityQuery& query, std::vector<ResolvedCapability>& out) const;
  Status query_fleet(const FleetCapabilityQuery& query, std::vector<ResolvedCapability>& out) const;
  Status query_stale(const StaleCapabilityQuery& query, std::vector<ResolvedCapability>& out) const;
  Status query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out) const;
  Status list_quirks(std::vector<QuirkRecord>& out) const;
  Status query_compatibility(const CompatibilityQuery& query, std::vector<CompatibilityFact>& out) const;
  Status compare_devices(const ComparisonRequest& request, DeviceComparison& out) const;
  Status snapshot(RegistrySnapshot& out) const;
  Status explain_capability(const CapabilityQuery& query, std::string& out) const;
  Status explain_device(const DeviceId& device, std::string& out) const;

  // -- epoch, revision, lifecycle ----------------------------------------
  [[nodiscard]] CoordinatorEpoch coordinator_epoch() const;
  Status advance_coordinator_epoch(CoordinatorEpoch& out);
  Status set_coordinator_epoch(CoordinatorEpoch epoch);
  [[nodiscard]] std::uint64_t state_revision() const;
  Status begin_shutdown();
  [[nodiscard]] bool shutting_down() const;
  Status clear_shutdown();

  // -- persistence --------------------------------------------------------
  DurableState export_durable_state() const;
  Status import_durable_state(const DurableState& state, ImportReport& report);

  [[nodiscard]] ResourceUsage resource_usage() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hcr
