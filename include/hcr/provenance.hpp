// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "hcr/ids.hpp"
#include "hcr/support.hpp"

namespace hcr {

/// Evidence source classes the registry can ingest. Vendor-specific sources
/// are isolated behind adapters; generic registry logic never branches on a
/// vendor, only on the declared authority of a source class.
enum class SourceClass {
  Unspecified = 0,
  SyntheticBackend,
  ImportedManifest,
  StaticCuratedDatabase,
  AcpiSmbios,
  PciConfiguration,
  OsEnumeration,
  NicDriverApi,
  CxlEnumeration,
  RocmSmi,
  Nvml,
  CudaRuntime,
  RuntimeProbe,
  FirmwareQuery,
  DriverQuery,
  LiveVendorApi,
  HardwareProbe,
};

const char* to_string(SourceClass value) noexcept;
bool source_class_from_string(std::string_view text, SourceClass& out) noexcept;

/// Deterministic source authority. Higher outranks lower.
///
/// Documented policy:
///   * an actively executed, cleaned-up hardware probe outranks every passive
///     query because it proves the capability on this device right now;
///   * passive live queries (vendor API, driver, firmware, runtime) outrank
///     enumeration;
///   * enumeration outranks curated/imported knowledge;
///   * curated databases outrank imported manifests, which outrank nothing but
///     the synthetic backend and unknown sources;
///   * the synthetic backend never outranks any real source.
int source_authority(SourceClass value) noexcept;

/// Provenance classification of a published fact.
enum class ProvenanceClass {
  RealLiveHardware = 0,
  RealOsReported,
  RealVendorApi,
  RealRuntimeProbe,
  CuratedStatic,
  Derived,
  Imported,
  Synthetic,
  UnknownSource,
};

const char* to_string(ProvenanceClass value) noexcept;
bool provenance_class_from_string(std::string_view text, ProvenanceClass& out) noexcept;

/// True when the provenance asserts a real (non-simulated) observation.
bool provenance_is_real(ProvenanceClass value) noexcept;

/// Durability policy: whether a fact may stay current across a coordinator
/// restart without fresh publication. Only externally curated/imported
/// knowledge is durable. Anything produced by a live process (vendor API,
/// driver query, enumeration, runtime probe, synthetic backend) is
/// process-bound and becomes REVALIDATION_REQUIRED after restart.
bool provenance_is_durable(ProvenanceClass value) noexcept;

/// Default provenance class implied by a source class. Publishers may not
/// claim a stronger provenance class than the source class implies; the
/// registry rejects such claims instead of silently trusting them.
ProvenanceClass default_provenance_for(SourceClass value) noexcept;

/// Total order on provenance strength; higher is a stronger claim.
int provenance_strength(ProvenanceClass value) noexcept;

/// True when a source class may legitimately publish the claimed provenance.
/// A source may weaken its claim, never strengthen it.
bool provenance_allowed_for(ProvenanceClass provenance, SourceClass source) noexcept;

/// Publisher authority. A publisher identity is bound to a boot identity and
/// the coordinator epoch that admitted it.
struct PublisherIdentity {
  PublisherId id;
  PublisherBootId boot;
  CoordinatorEpoch epoch;

  friend bool operator==(const PublisherIdentity& a, const PublisherIdentity& b) noexcept {
    return a.id == b.id && a.boot == b.boot && a.epoch == b.epoch;
  }
};

/// Lifecycle of a publisher as tracked by the coordinator.
enum class PublisherState {
  Unknown = 0,
  Registered,
  Fenced,
  Dead,
};

const char* to_string(PublisherState value) noexcept;

/// Fully attributed evidence origin.
struct EvidenceSource {
  SourceClass source_class = SourceClass::Unspecified;
  ProvenanceClass provenance = ProvenanceClass::UnknownSource;
  PublisherIdentity publisher;
  std::string adapter;            ///< e.g. "nvml", "cpu.windows", "synthetic.backend"
  std::string native_reference;   ///< e.g. "nvmlDeviceGetMigMode"
  std::uint64_t observation_sequence = 0;
  std::string observed_at_utc;    ///< ISO-8601 seconds precision
  EvidenceGeneration evidence_generation;
  std::uint64_t platform_generation = 0;

  [[nodiscard]] int authority() const noexcept { return source_authority(source_class); }
  [[nodiscard]] bool live() const noexcept { return provenance_is_real(provenance); }
  [[nodiscard]] bool durable() const noexcept { return provenance_is_durable(provenance); }
};

/// Reasons an evidence record is no longer authoritative for the current
/// device state. A record may accumulate several reasons.
enum class StalenessFlag : std::uint32_t {
  None = 0,
  DeviceGenerationSuperseded = 1u << 0,
  DeviceBootChanged = 1u << 1,
  DriverGenerationChanged = 1u << 2,
  FirmwareGenerationChanged = 1u << 3,
  RuntimeGenerationChanged = 1u << 4,
  PlatformGenerationChanged = 1u << 5,
  PublisherFenced = 1u << 6,
  EpochSuperseded = 1u << 7,
  SupersededByNewerEvidence = 1u << 8,
  Withdrawn = 1u << 9,
  RequiresRevalidation = 1u << 10,
};

constexpr StalenessFlag operator|(StalenessFlag a, StalenessFlag b) noexcept {
  return static_cast<StalenessFlag>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr StalenessFlag operator&(StalenessFlag a, StalenessFlag b) noexcept {
  return static_cast<StalenessFlag>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
StalenessFlag& operator|=(StalenessFlag& a, StalenessFlag b) noexcept;
bool is_stale(StalenessFlag flags) noexcept;
std::string describe_staleness(StalenessFlag flags);

/// A short single-word reason used in query results and explanations.
const char* staleness_code(StalenessFlag flags) noexcept;

}  // namespace hcr
