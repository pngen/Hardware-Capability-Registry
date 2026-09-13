// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hcr/hardware.hpp"
#include "hcr/provenance.hpp"
#include "hcr/publication.hpp"
#include "hcr/status.hpp"

namespace hcr {

class CapabilityRegistry;
class CapabilityPublisher;

/// Everything a discovery adapter needs in order to publish. Generations are
/// assigned by the registry, never by the adapter.
struct DiscoveryContext {
  PublisherIdentity publisher;
  PlatformIdentity platform;
  EvidenceGeneration evidence_generation;
  std::string observed_at_utc;
  /// Facts the adapter discovered that qualify capabilities (driver version,
  /// os, kernel, feature gates). Used for conditional evaluation and for
  /// describing the device state.
  EnvironmentContext environment;
  DeviceSoftwareState software;
  /// Source class and provenance applied to every publication from this
  /// context. A publisher may never publish stronger provenance than its
  /// source class implies; the registry rejects such claims.
  SourceClass source_class = SourceClass::Unspecified;
  ProvenanceClass provenance = ProvenanceClass::UnknownSource;
  /// Adapter name recorded in the provenance of every publication built from
  /// this context. Set by the discovery harness from the backend itself.
  std::string adapter;
};

/// Destination for evidence. Both the local registry and a remote coordinator
/// are reached through this interface, so an adapter cannot tell the
/// difference and no test-only shortcut path exists.
class EvidenceSink {
 public:
  virtual ~EvidenceSink();

  /// Ensures the sink holds the given publisher identity and returns the
  /// identity actually in force (the registry assigns the boot identity).
  virtual ApplyResult ensure_publisher(const PublisherIdentity& identity, std::string adapter) = 0;

  /// Issues the next sequence number for a publisher. The sequence stream
  /// belongs to the publisher, not to one discovery run, so several adapters
  /// publishing through one sink never replay a sequence number.
  virtual std::uint64_t next_sequence(const PublisherId& publisher) = 0;

  virtual ApplyResult publish(const Publication& publication) = 0;
};

/// In-process sink writing directly to a registry through the production
/// ingestion path.
///
/// The sink is one process, so it holds one boot identity per publisher
/// identifier: repeatedly ensuring the same publisher returns the identity
/// already assigned instead of performing a reincarnation that would fence the
/// evidence this very process just published.
class LocalEvidenceSink final : public EvidenceSink {
 public:
  explicit LocalEvidenceSink(CapabilityRegistry& registry) : registry_(registry) {}

  ApplyResult ensure_publisher(const PublisherIdentity& identity, std::string adapter) override;
  std::uint64_t next_sequence(const PublisherId& publisher) override;
  ApplyResult publish(const Publication& publication) override;

  [[nodiscard]] std::size_t publisher_count() const;

 private:
  CapabilityRegistry& registry_;
  mutable std::mutex mutex_;
  std::map<PublisherId, PublisherIdentity> assigned_;
  std::map<PublisherId, std::uint64_t> sequences_;
};

/// Outcome of running one discovery backend. Rejections and adapter notes are
/// recorded rather than swallowed.
struct DiscoveryReport {
  std::string adapter;
  bool available = false;
  std::size_t accepted = 0;
  std::size_t idempotent = 0;
  std::size_t rejected = 0;
  std::vector<std::string> notes;
};

/// Narrow discovery adapter. Adapters only gather facts and submit immutable
/// publications; they never mutate registry internals and never claim support
/// they cannot prove.
class IDiscoveryBackend {
 public:
  virtual ~IDiscoveryBackend();

  /// Stable adapter name recorded in provenance, e.g. "nvml".
  virtual std::string adapter_name() const = 0;
  virtual SourceClass source_class() const = 0;
  virtual ProvenanceClass provenance() const = 0;

  /// True when the backend can run on this host right now.
  virtual bool available(std::string& reason) const = 0;

  /// Discovers facts and submits them through the sink. Must not throw. The
  /// report records what was accepted and rejected; rejections are surfaced,
  /// never swallowed.
  virtual Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) = 0;
};

/// Deterministic device identity derived from a canonical kind and token, so
/// independent adapters agree on exactly one subject.
DeviceId canonical_device_id(std::string_view kind, std::string_view token);

/// Deterministic identity of the host platform subject. Every host-scoped
/// adapter (CPU, PCI, NIC, CXL) contributes capabilities to this one subject
/// instead of inventing private hosts.
DeviceId host_device_id(const PlatformIdentity& platform);

/// A device subject as assigned by the registry.
struct SubjectContext {
  DeviceId device;
  DeviceGeneration generation;
  DeviceBootId boot;
};

/// Deterministic builder used by discovery adapters.
///
/// It owns sequence and evidence generation counters, binds every publication
/// to one publisher identity, records accept/reject outcomes in a report and
/// never mutates registry internals. Adapters therefore cannot bypass the
/// production ingestion path.
class PublicationBuilder {
 public:
  PublicationBuilder(EvidenceSink& sink, DiscoveryContext context);

  /// Registers (or refreshes) a device subject and returns the generations the
  /// registry assigned. Rejections are reported, never ignored.
  Status register_subject(const DeviceRegistration& registration, SubjectContext& out);
  Status announce_device_generation(const DeviceRegistration& registration, SubjectContext& out);

  /// Publishes one capability observation. Source class and provenance default
  /// to the adapter's own classification; a backend may weaken them explicitly
  /// (for example when it publishes curated knowledge alongside live evidence),
  /// but it can never strengthen them past what the source class implies.
  Status publish_capability(const SubjectContext& subject,
                            const CapabilityId& capability,
                            SupportState support,
                            CapabilityValue value,
                            PrecisionClass precision,
                            std::string native_reference,
                            ConditionPtr conditions = nullptr,
                            SourceClass source_class = SourceClass::Unspecified,
                            ProvenanceClass provenance = ProvenanceClass::UnknownSource);

  Status publish_quirk(const QuirkRecord& quirk);
  Status publish_compatibility(const CompatibilityFact& compatibility);
  Status register_schema(const CapabilitySchema& schema);
  Status heartbeat();

  /// Advances the evidence generation. Used when a publisher deliberately
  /// re-observes a device so that stale evidence can be told apart.
  void bump_evidence_generation();
  [[nodiscard]] EvidenceGeneration evidence_generation() const noexcept { return context_.evidence_generation; }
  [[nodiscard]] const DiscoveryContext& context() const noexcept { return context_; }
  [[nodiscard]] const DiscoveryReport& report() const noexcept { return report_; }
  [[nodiscard]] DiscoveryReport& report() noexcept { return report_; }

 private:
  Status submit(Publication publication);
  std::uint64_t take_sequence();
  ApplyResult note(ApplyResult result, const char* what);

  EvidenceSink& sink_;
  DiscoveryContext context_;
  DiscoveryReport report_;
};

/// Runs one backend and records what it published. Rejections are reported,
/// never silently swallowed.
Status run_discovery(IDiscoveryBackend& backend,
                     const DiscoveryContext& context,
                     EvidenceSink& sink,
                     DiscoveryReport& report);

}  // namespace hcr
