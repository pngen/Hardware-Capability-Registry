// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/registry.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"
#include "hcr/version.hpp"

namespace hcr {

// ---------------------------------------------------------------------------
// Publication helpers
// ---------------------------------------------------------------------------

const char* to_string(PublicationKind kind) noexcept {
  switch (kind) {
    case PublicationKind::RegisterPublisher: return "register-publisher";
    case PublicationKind::RegisterDevice: return "register-device";
    case PublicationKind::PublishDeviceGeneration: return "publish-device-generation";
    case PublicationKind::PublishCapability: return "publish-capability";
    case PublicationKind::WithdrawCapability: return "withdraw-capability";
    case PublicationKind::PublishQuirk: return "publish-quirk";
    case PublicationKind::WithdrawQuirk: return "withdraw-quirk";
    case PublicationKind::PublishCompatibility: return "publish-compatibility";
    case PublicationKind::WithdrawCompatibility: return "withdraw-compatibility";
    case PublicationKind::RegisterCapabilitySchema: return "register-capability-schema";
    case PublicationKind::Heartbeat: return "heartbeat";
  }
  return "register-device";
}

bool publication_kind_from_string(std::string_view text, PublicationKind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(PublicationKind::Heartbeat); ++i) {
    const auto kind = static_cast<PublicationKind>(i);
    if (equals_ascii_ci(text, to_string(kind))) {
      out = kind;
      return true;
    }
  }
  return false;
}

const char* to_string(ApplyCode code) noexcept {
  switch (code) {
    case ApplyCode::Accepted: return "accepted";
    case ApplyCode::AcceptedIdempotent: return "accepted-idempotent";
    case ApplyCode::RejectedValidation: return "rejected-validation";
    case ApplyCode::RejectedUnknownPublisher: return "rejected-unknown-publisher";
    case ApplyCode::RejectedFencedPublisher: return "rejected-fenced-publisher";
    case ApplyCode::RejectedStaleBoot: return "rejected-stale-boot";
    case ApplyCode::RejectedStaleEpoch: return "rejected-stale-epoch";
    case ApplyCode::RejectedStaleSequence: return "rejected-stale-sequence";
    case ApplyCode::RejectedGenerationRegression: return "rejected-generation-regression";
    case ApplyCode::RejectedDuplicateConflict: return "rejected-duplicate-conflict";
    case ApplyCode::RejectedStaleDeviceGeneration: return "rejected-stale-device-generation";
    case ApplyCode::RejectedStaleCapabilityGeneration: return "rejected-stale-capability-generation";
    case ApplyCode::RejectedUnknownDevice: return "rejected-unknown-device";
    case ApplyCode::RejectedUnknownSchema: return "rejected-unknown-schema";
    case ApplyCode::RejectedLimit: return "rejected-limit";
    case ApplyCode::RejectedBackpressure: return "rejected-backpressure";
    case ApplyCode::RejectedShuttingDown: return "rejected-shutting-down";
    case ApplyCode::RejectedWithdrawnSubject: return "rejected-withdrawn-subject";
    case ApplyCode::RejectedInvariant: return "rejected-invariant";
  }
  return "rejected-validation";
}

bool apply_code_accepted(ApplyCode code) noexcept {
  return code == ApplyCode::Accepted || code == ApplyCode::AcceptedIdempotent;
}

Status validate_publication(const Publication& publication, const RegistryLimits& limits) {
  if (!publication.publisher.id.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "publication requires a publisher identity");
  }
  if (!publication.publisher.boot.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "publication requires a publisher boot identity");
  }
  if (publication.issued_at_utc.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "publication timestamp exceeds length limit");
  }
  if (publication.reason.size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "publication reason exceeds length limit");
  }
  switch (publication.kind) {
    case PublicationKind::RegisterPublisher:
    case PublicationKind::Heartbeat:
      return Status::success();
    case PublicationKind::RegisterDevice:
    case PublicationKind::PublishDeviceGeneration:
      return validate_device_identity(publication.device.identity, limits);
    case PublicationKind::PublishCapability:
    case PublicationKind::WithdrawCapability: {
      if (publication.capability.withdrawn != (publication.kind == PublicationKind::WithdrawCapability)) {
        return Status::failure(ErrorCode::InvalidArgument,
                               "capability publication kind and withdrawal flag disagree");
      }
      return validate_capability_record(publication.capability, limits);
    }
    case PublicationKind::PublishQuirk:
    case PublicationKind::WithdrawQuirk:
      return validate_quirk_record(publication.quirk, limits);
    case PublicationKind::PublishCompatibility:
    case PublicationKind::WithdrawCompatibility: {
      if (publication.compatibility.withdrawn != (publication.kind == PublicationKind::WithdrawCompatibility)) {
        return Status::failure(ErrorCode::InvalidArgument,
                               "compatibility publication kind and withdrawal flag disagree");
      }
      return validate_compatibility_fact(publication.compatibility, limits);
    }
    case PublicationKind::RegisterCapabilitySchema:
      return validate_capability_schema(publication.schema, limits);
  }
  return Status::failure(ErrorCode::InvalidArgument, "unknown publication kind");
}

// ---------------------------------------------------------------------------
// Registry implementation
// ---------------------------------------------------------------------------

namespace {

using KeyId = std::pair<DeviceId, CapabilityId>;

struct StoredRecord {
  CapabilityRecord record;
  /// Flags that belong to the stored record itself rather than being derived
  /// from current device/publisher state.
  StalenessFlag persistent = StalenessFlag::None;
  std::string slot;
  std::uint64_t serial = 0;
};

struct KeyState {
  CapabilitySchemaId schema;
  CapabilityGeneration generation;
  bool has_evidence = false;
  std::vector<StoredRecord> records;
};

struct DeviceState {
  DeviceIdentity identity;
  DeviceSoftwareState software;
  DeviceGeneration generation;
  DeviceBootId boot;
  PlatformGeneration platform_generation;
};

struct PublisherStateInternal {
  PublisherRecord record;
  std::set<PublisherBootId> fenced_boots;
  std::map<PublisherBootId, PublisherWatermark> watermarks;
  std::map<std::uint64_t, std::uint64_t> sequence_fingerprints;
  std::size_t boot_history = 0;
  bool publisher_fenced = false;
};

struct GlobalEntry {
  KeyId key;
  std::uint64_t serial = 0;
};

std::string fingerprint_publication(const Publication& publication) {
  std::uint64_t hash = kFnvOffsetBasis;
  fnv1a64_mix(hash, to_string(publication.kind));
  fnv1a64_mix(hash, publication.capability.device.str());
  fnv1a64_mix(hash, publication.capability.capability.str());
  fnv1a64_mix(hash, publication.capability.schema.str());
  fnv1a64_mix(hash, static_cast<std::uint64_t>(publication.capability.support));
  fnv1a64_mix(hash, publication.capability.value.describe());
  fnv1a64_mix(hash, describe_condition(publication.capability.conditions.get()));
  fnv1a64_mix(hash, publication.capability.withdrawn ? "withdrawn" : "present");
  fnv1a64_mix(hash, publication.quirk.id.str());
  fnv1a64_mix(hash, std::to_string(publication.quirk.quirk_generation.value()));
  fnv1a64_mix(hash, publication.quirk.title);
  fnv1a64_mix(hash, publication.compatibility.id.str());
  fnv1a64_mix(hash, std::to_string(publication.compatibility.compatibility_generation.value()));
  fnv1a64_mix(hash, publication.compatibility.lhs.token);
  fnv1a64_mix(hash, publication.compatibility.rhs.token);
  fnv1a64_mix(hash, publication.schema.id.str());
  fnv1a64_mix(hash, publication.device.identity.id.str());
  fnv1a64_mix(hash, publication.device.identity.incarnation.str());
  fnv1a64_mix(hash, publication.device.software.driver.version);
  fnv1a64_mix(hash, publication.device.software.firmware.version);
  fnv1a64_mix(hash, publication.device.software.runtime.version);
  return hex64(hash);
}

bool capability_content_equal(const CapabilityRecord& left, const CapabilityRecord& right) {
  return left.support == right.support && left.value == right.value &&
         describe_condition(left.conditions.get()) == describe_condition(right.conditions.get());
}

bool condition_tree_contains(const ConditionNode* root, ConditionKind axis) {
  if (root == nullptr) return false;
  std::vector<const ConditionNode*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    const ConditionNode* node = stack.back();
    stack.pop_back();
    if (node == nullptr) continue;
    if (node->kind == axis) return true;
    for (const auto& child : node->children) stack.push_back(child.get());
  }
  return false;
}

bool record_gate_axis(const CapabilityRecord& record, ConditionKind axis) {
  return condition_tree_contains(record.conditions.get(), axis);
}

bool view_gate_axis(const ResolvedCapability& view, ConditionKind axis) {
  return condition_tree_contains(view.conditions.get(), axis);
}

const char* condition_axis_name(ConditionKind kind) noexcept {
  switch (kind) {
    case ConditionKind::MinDriverVersion: return "driver";
    case ConditionKind::MinFirmwareVersion: return "firmware";
    case ConditionKind::MinRuntimeVersion: return "runtime";
    default: return "unknown";
  }
}

}  // namespace

struct CapabilityRegistry::Impl {
  explicit Impl(RegistryLimits registry_limits) : limits(registry_limits) {}

  RegistryLimits limits;
  mutable std::mutex mutex;

  std::map<CapabilitySchemaId, CapabilitySchema> schemas;
  std::map<CapabilityId, CapabilitySchemaId> capability_schemas;

  std::map<DeviceId, DeviceState> devices;
  std::map<PlatformId, PlatformIdentity> platforms;
  std::map<KeyId, KeyState> keys;
  /// Per-device index of capability keys. Every device-scoped query walks this
  /// index instead of the whole key space, so listing one device's capabilities
  /// and comparing two devices stay proportional to those devices rather than
  /// to the size of the registry.
  std::map<DeviceId, std::set<CapabilityId>> device_keys;
  std::set<HardwareFamilyId> families;
  std::set<HardwareModelId> models;
  std::set<ArchitectureId> architectures;

  std::deque<GlobalEntry> global_order;
  std::size_t evidence_count = 0;
  std::uint64_t next_serial = 1;

  std::map<QuirkId, QuirkRecord> quirks;
  std::map<CompatibilityFactId, CompatibilityFact> compatibility;
  std::map<PublisherId, PublisherStateInternal> publishers;

  CoordinatorEpoch epoch;
  SnapshotGeneration snapshot_generation;
  std::uint64_t state_revision = 0;
  bool shutting_down = false;
  ResourceUsage usage;

  // -- helpers ------------------------------------------------------------

  static PublisherWatermark watermark_for(const PublisherId& id, const PublisherStateInternal& publisher) {
    PublisherWatermark watermark;
    watermark.publisher = id;
    watermark.boot = publisher.record.identity.boot;
    watermark.max_sequence = publisher.record.last_sequence;
    watermark.max_evidence_generation = publisher.record.max_evidence_generation;
    watermark.state = publisher.record.state;
    watermark.adapter = publisher.record.adapter;
    watermark.reason = publisher.record.reason;
    watermark.accepted = publisher.record.accepted;
    watermark.rejected = publisher.record.rejected;
    watermark.registered_at_utc = publisher.record.registered_at_utc;
    return watermark;
  }

  const CapabilitySchema* find_schema_locked(const CapabilitySchemaId& id) const {
    const auto it = schemas.find(id);
    if (it == schemas.end()) return nullptr;
    return &it->second;
  }

  DeviceState* find_device_locked(const DeviceId& id) {
    const auto it = devices.find(id);
    if (it == devices.end()) return nullptr;
    return &it->second;
  }

  const DeviceState* find_device_locked(const DeviceId& id) const {
    const auto it = devices.find(id);
    if (it == devices.end()) return nullptr;
    return &it->second;
  }

  bool publisher_is_authoritative(const CapabilityRecord& record) const {
    const auto it = publishers.find(record.source.publisher.id);
    if (it == publishers.end()) return false;
    const PublisherStateInternal& state = it->second;
    if (state.publisher_fenced) return false;
    if (state.record.state != PublisherState::Registered) return false;
    if (state.record.identity.boot != record.source.publisher.boot) return false;
    if (state.fenced_boots.count(record.source.publisher.boot) != 0) return false;
    return true;
  }

  StalenessFlag compute_staleness_locked(const StoredRecord& stored,
                                         const DeviceState& device,
                                         DeviceGeneration target,
                                         bool target_is_current) const {
    StalenessFlag flags = stored.persistent;
    const CapabilityRecord& record = stored.record;

    if (record.withdrawn) {
      // A withdrawn claim is retained for audit but is not current evidence:
      // withdrawing evidence is not the same as asserting non-support.
      flags |= StalenessFlag::Withdrawn;
    }
    if (record.device_generation != target) {
      flags |= StalenessFlag::DeviceGenerationSuperseded;
    }
    if (!target_is_current) {
      flags |= StalenessFlag::DeviceGenerationSuperseded;
    }
    if (target_is_current) {
      if (record.device_boot != device.boot) {
        flags |= StalenessFlag::DeviceBootChanged;
      }
      const bool durable = provenance_is_durable(record.source.provenance);
      const bool live_bound = !durable;
      // An axis invalidates evidence only when the evidence was observed under
      // an identified generation of that axis which has since advanced.
      // Evidence gathered before an axis was identified is not retroactively
      // invalidated by the axis later becoming known.
      if (live_bound || record_gate_axis(record, ConditionKind::MinDriverVersion)) {
        if (record.driver_generation.valid() &&
            record.driver_generation != device.software.driver_generation) {
          flags |= StalenessFlag::DriverGenerationChanged;
        }
      }
      if (live_bound || record_gate_axis(record, ConditionKind::MinFirmwareVersion)) {
        if (record.firmware_generation.valid() &&
            record.firmware_generation != device.software.firmware_generation) {
          flags |= StalenessFlag::FirmwareGenerationChanged;
        }
      }
      if (live_bound || record_gate_axis(record, ConditionKind::MinRuntimeVersion)) {
        // Many runtime components can expose one device, so a runtime change
        // invalidates only evidence observed through that same runtime.
        const bool same_runtime = record.runtime_id.valid() && record.runtime_id == device.software.runtime.id;
        if (same_runtime && record.runtime_generation.valid() &&
            record.runtime_generation != device.software.runtime_generation) {
          flags |= StalenessFlag::RuntimeGenerationChanged;
        }
      }
      if (live_bound && record.platform_generation.valid() &&
          record.platform_generation != device.platform_generation) {
        flags |= StalenessFlag::PlatformGenerationChanged;
      }
      if (live_bound) {
        if (!publisher_is_authoritative(record)) {
          // A publisher that is unregistered, dead, fenced, or superseded by a
          // newer boot identity permanently loses authority for this evidence.
          flags |= StalenessFlag::PublisherFenced;
        }
        if (record.source.publisher.epoch != epoch) {
          flags |= StalenessFlag::EpochSuperseded;
        }
      }
    }
    return flags;
  }

  static bool evidence_ordering(const EvidenceView& left, const EvidenceView& right) {
    if (left.staleness != right.staleness) return is_stale(left.staleness) < is_stale(right.staleness);
    if (left.winner != right.winner) return left.winner;
    if (left.capability_generation != right.capability_generation) {
      return left.capability_generation > right.capability_generation;
    }
    const int left_authority = left.source.authority();
    const int right_authority = right.source.authority();
    if (left_authority != right_authority) return left_authority > right_authority;
    if (left.source.publisher.id != right.source.publisher.id) {
      return left.source.publisher.id < right.source.publisher.id;
    }
    return left.source.observation_sequence > right.source.observation_sequence;
  }

  ResolvedCapability resolve_locked(const DeviceState& device,
                                    const CapabilityId& capability,
                                    DeviceGeneration pinned,
                                    const EnvironmentContext& environment) const {
    ResolvedCapability resolved;
    resolved.device = device.identity.id;
    resolved.device_generation = device.generation;
    resolved.device_boot = device.boot;
    resolved.capability = capability;
    resolved.firmware_generation = device.software.firmware_generation;
    resolved.driver_generation = device.software.driver_generation;
    resolved.runtime_generation = device.software.runtime_generation;

    const DeviceGeneration target = pinned.valid() ? pinned : device.generation;
    const bool target_is_current = target == device.generation;
    resolved.current = target_is_current;

    const auto key_it = keys.find(KeyId{device.identity.id, capability});
    if (key_it == keys.end()) {
      resolved.known = false;
      resolved.declared_support = SupportState::Unknown;
      resolved.effective_support = EffectiveSupport::Unknown;
      resolved.truth = TruthClass::Unknown;
      resolved.contradiction = ContradictionState::Consistent;
      // No knowledge is recorded for this key, so the answer is not a current
      // capability statement; it is an explicit UNKNOWN.
      resolved.current = false;
      resolved.explanation =
          std::string("capability ") + capability.str() + " on device " + device.identity.id.str() +
          ": no capability knowledge recorded; state UNKNOWN (absence of evidence is not evidence of absence)";
      return resolved;
    }

    const KeyState& key = key_it->second;
    resolved.known = true;
    resolved.schema = key.schema;

    struct Candidate {
      const StoredRecord* stored;
      StalenessFlag flags;
      std::string assertion;
    };
    std::vector<Candidate> candidates;
    std::vector<EvidenceView> evidence;
    std::vector<const StoredRecord*> record_order;
    evidence.reserve(key.records.size());
    record_order.reserve(key.records.size());

    StalenessFlag any_flags = StalenessFlag::None;
    for (const StoredRecord& stored : key.records) {
      const StalenessFlag flags = compute_staleness_locked(stored, device, target, target_is_current);
      any_flags |= flags;
      EvidenceView view;
      view.capability_generation = stored.record.capability_generation;
      view.support = stored.record.support;
      view.value = stored.record.value;
      view.precision = stored.record.precision;
      view.source = stored.record.source;
      view.staleness = flags;
      view.withdrawn = stored.record.withdrawn;
      evidence.push_back(view);
      record_order.push_back(&stored);
      if (flags == StalenessFlag::None) {
        Candidate candidate;
        candidate.stored = &stored;
        candidate.flags = flags;
        candidate.assertion = std::string(to_string(stored.record.support)) + "|" +
                              stored.record.value.describe() + "|" +
                              describe_condition(stored.record.conditions.get());
        candidates.push_back(candidate);
      }
    }

    const auto by_authority = [](const Candidate& left, const Candidate& right) {
      const int left_authority = left.stored->record.source.authority();
      const int right_authority = right.stored->record.source.authority();
      if (left_authority != right_authority) return left_authority > right_authority;
      const int left_precision = precision_class_rank(left.stored->record.precision);
      const int right_precision = precision_class_rank(right.stored->record.precision);
      if (left_precision != right_precision) return left_precision > right_precision;
      if (left.stored->record.source.evidence_generation != right.stored->record.source.evidence_generation) {
        return left.stored->record.source.evidence_generation >
               right.stored->record.source.evidence_generation;
      }
      if (left.stored->record.source.publisher.id != right.stored->record.source.publisher.id) {
        return left.stored->record.source.publisher.id < right.stored->record.source.publisher.id;
      }
      return left.stored->record.source.observation_sequence >
             right.stored->record.source.observation_sequence;
    };

    if (candidates.empty()) {
      // No current evidence. Report the newest retained assertion as declared
      // knowledge but never as current support.
      const StoredRecord* newest = nullptr;
      for (const StoredRecord& stored : key.records) {
        if (stored.record.withdrawn) continue;
        if (newest == nullptr ||
            stored.record.capability_generation > newest->record.capability_generation ||
            (stored.record.capability_generation == newest->record.capability_generation &&
             stored.serial > newest->serial)) {
          newest = &stored;
        }
      }
      // The answer describes a device generation whose evidence is no longer
      // authoritative, so it is explicitly not current.
      resolved.current = false;
      resolved.contradiction = ContradictionState::RevalidationRequired;
      resolved.staleness = any_flags == StalenessFlag::None ? StalenessFlag::RequiresRevalidation : any_flags;
      if (newest != nullptr) {
        resolved.declared_support = newest->record.support;
        resolved.value = newest->record.value;
        resolved.precision = newest->record.precision;
        resolved.conditions = newest->record.conditions;
        resolved.condition_evaluation = evaluate_condition(newest->record.conditions.get(), environment);
      } else {
        resolved.declared_support = SupportState::Unknown;
      }
      resolved.effective_support = EffectiveSupport::RevalidationRequired;
      resolved.truth = key.has_evidence ? TruthClass::Real : TruthClass::Unknown;
      resolved.capability_generation = key.generation;
      resolved.explanation = std::string("capability ") + capability.str() + " on device " +
                             device.identity.id.str() + ": no current evidence (" +
                             describe_staleness(resolved.staleness) +
                             "); declared knowledge is retained for audit but is not authoritative";
      std::sort(evidence.begin(), evidence.end(), evidence_ordering);
      resolved.evidence = std::move(evidence);
      resolved.quirks = gather_quirks_locked(device, capability, environment);
      return resolved;
    }

    std::size_t winner_index = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
      if (by_authority(candidates[i], candidates[winner_index])) winner_index = i;
    }

    bool all_equal = true;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
      if (candidates[i].assertion != candidates[0].assertion) {
        all_equal = false;
        break;
      }
    }

    if (all_equal) {
      resolved.contradiction = ContradictionState::Consistent;
    } else {
      const int top_authority = candidates[winner_index].stored->record.source.authority();
      std::vector<std::size_t> top;
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].stored->record.source.authority() == top_authority) top.push_back(i);
      }
      bool top_agree = true;
      for (const std::size_t index : top) {
        if (candidates[index].assertion != candidates[top.front()].assertion) {
          top_agree = false;
          break;
        }
      }
      if (!top_agree) {
        resolved.contradiction = ContradictionState::Ambiguous;
      } else {
        const bool winner_live = provenance_is_real(candidates[winner_index].stored->record.source.provenance);
        bool dissent_all_non_live = true;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
          if (i == winner_index) continue;
          if (candidates[i].assertion == candidates[winner_index].assertion) continue;
          if (provenance_is_real(candidates[i].stored->record.source.provenance)) {
            dissent_all_non_live = false;
            break;
          }
        }
        if (winner_live && dissent_all_non_live) {
          resolved.contradiction = ContradictionState::PreferredLiveEvidence;
        } else {
          resolved.contradiction = ContradictionState::PreferredHigherAuthoritySource;
        }
      }
    }

    const StoredRecord& winner = *candidates[winner_index].stored;
    // The answer carries the key's current capability generation rather than
    // the winning record's own generation: evidence that arrives later never
    // makes an answer regress, regardless of publication order.
    resolved.capability_generation = key.generation;
    resolved.declared_support = winner.record.support;
    resolved.value = winner.record.value;
    resolved.precision = winner.record.precision;
    resolved.conditions = winner.record.conditions;
    resolved.condition_evaluation = evaluate_condition(winner.record.conditions.get(), environment);
    resolved.staleness = StalenessFlag::None;

    for (EvidenceView& view : evidence) {
      view.winner = view.source.publisher.id == winner.record.source.publisher.id &&
                    view.capability_generation == winner.record.capability_generation &&
                    view.source.native_reference == winner.record.source.native_reference &&
                    view.source.observation_sequence == winner.record.source.observation_sequence;
    }

    // Evidence is attached first: truth classification and the explanation are
    // both derived from the retained evidence, including losing records.
    std::sort(evidence.begin(), evidence.end(), evidence_ordering);
    resolved.evidence = std::move(evidence);
    resolved.quirks = gather_quirks_locked(device, capability, environment);
    resolved.effective_support = compute_effective_support(resolved);
    resolved.truth = classify_truth(resolved);
    resolved.explanation = build_explanation(resolved, device, target_is_current);
    return resolved;
  }

  std::vector<QuirkApplication> gather_quirks_locked(const DeviceState& device,
                                                     const CapabilityId& capability,
                                                     const EnvironmentContext& environment) const {
    std::vector<QuirkApplication> applications;
    std::vector<QuirkApplication> deferred;
    for (const auto& entry : quirks) {
      const QuirkRecord& quirk = entry.second;
      if (quirk.withdrawn) continue;
      if (std::find(quirk.impacted_capabilities.begin(), quirk.impacted_capabilities.end(), capability) ==
          quirk.impacted_capabilities.end()) {
        continue;
      }
      QuirkApplication application =
          evaluate_quirk(quirk, device.identity, device.software, environment);
      if (application.result == QuirkApplicabilityResult::DoesNotApply) continue;
      if (application.result == QuirkApplicabilityResult::Unknown) {
        deferred.push_back(std::move(application));
      } else {
        applications.push_back(std::move(application));
      }
      if (applications.size() + deferred.size() >= limits.max_query_results) break;
    }
    const auto order = [](const QuirkApplication& left, const QuirkApplication& right) {
      if (left.severity != right.severity) return static_cast<int>(left.severity) > static_cast<int>(right.severity);
      if (left.id != right.id) return left.id < right.id;
      return left.quirk_generation < right.quirk_generation;
    };
    std::sort(applications.begin(), applications.end(), order);
    std::sort(deferred.begin(), deferred.end(), order);
    applications.insert(applications.end(), deferred.begin(), deferred.end());
    return applications;
  }

  static bool quirk_blocks(const QuirkApplication& application) {
    return application.result == QuirkApplicabilityResult::Applies &&
           (application.severity == QuirkSeverity::Unusable || application.severity == QuirkSeverity::Fatal);
  }

  static bool quirk_caveats(const QuirkApplication& application) {
    if (application.result != QuirkApplicabilityResult::Applies) return false;
    return application.severity == QuirkSeverity::Caveat || application.severity == QuirkSeverity::Degraded;
  }

  static EffectiveSupport compute_effective_support(const ResolvedCapability& resolved) {
    switch (resolved.declared_support) {
      case SupportState::Unknown:
        return EffectiveSupport::Unknown;
      case SupportState::RevalidationRequired:
        return EffectiveSupport::RevalidationRequired;
      case SupportState::Unsupported:
        return EffectiveSupport::Unsupported;
      case SupportState::Disabled:
        return EffectiveSupport::Disabled;
      default:
        break;
    }
    switch (resolved.condition_evaluation.outcome) {
      case ConditionOutcome::Unsatisfied:
        return EffectiveSupport::SupportedConditionalUnmet;
      case ConditionOutcome::Unknown:
        return EffectiveSupport::SupportedConditionalUnknown;
      default:
        break;
    }
    for (const QuirkApplication& application : resolved.quirks) {
      if (quirk_blocks(application)) return EffectiveSupport::BlockedByQuirk;
    }
    for (const QuirkApplication& application : resolved.quirks) {
      if (quirk_caveats(application)) return EffectiveSupport::SupportedWithCaveat;
    }
    return EffectiveSupport::Supported;
  }

  static TruthClass classify_truth(const ResolvedCapability& resolved) {
    if (resolved.declared_support == SupportState::Unsupported ||
        resolved.effective_support == EffectiveSupport::Unsupported) {
      return TruthClass::Unsupported;
    }
    if (resolved.effective_support == EffectiveSupport::Unknown &&
        resolved.declared_support == SupportState::Unknown) {
      return TruthClass::Unknown;
    }
    for (const EvidenceView& view : resolved.evidence) {
      if (view.winner) {
        switch (view.source.provenance) {
          case ProvenanceClass::Synthetic: return TruthClass::Synthetic;
          case ProvenanceClass::UnknownSource: return TruthClass::Unknown;
          default: return TruthClass::Real;
        }
      }
    }
    return TruthClass::Unknown;
  }

  std::string build_explanation(const ResolvedCapability& resolved,
                                const DeviceState& device,
                                bool target_is_current) const {
    std::string out;
    out += "capability ";
    out += resolved.capability.str();
    out += " on device ";
    out += device.identity.id.str();
    out += " (generation ";
    out += device.generation.str();
    out += target_is_current ? ", current)" : ", historical)";
    out += "\n  declared: ";
    out += to_string(resolved.declared_support);
    out += "\n  effective: ";
    out += to_string(resolved.effective_support);
    if (!resolved.value.empty()) {
      out += "\n  value: ";
      out += resolved.value.describe();
    }
    out += "\n  truth: ";
    out += to_string(resolved.truth);
    out += "\n  precision: ";
    out += to_string(resolved.precision);
    out += "\n  contradiction: ";
    out += to_string(resolved.contradiction);
    out += "\n  staleness: ";
    out += describe_staleness(resolved.staleness);
    if (resolved.conditions != nullptr) {
      out += "\n  conditions: ";
      out += describe_condition(resolved.conditions.get());
      out += " => ";
      out += to_string(resolved.condition_evaluation.outcome);
    }
    for (const QuirkApplication& application : resolved.quirks) {
      out += "\n  quirk ";
      out += application.id.str();
      out += " [";
      out += to_string(application.severity);
      out += "/";
      out += to_string(application.result);
      out += "]: ";
      out += application.reason;
    }
    for (const EvidenceView& view : resolved.evidence) {
      out += "\n  evidence ";
      out += view.source.adapter;
      if (!view.source.native_reference.empty()) {
        out += " reference=";
        out += view.source.native_reference;
      }
      out += " source=";
      out += to_string(view.source.source_class);
      out += " provenance=";
      out += to_string(view.source.provenance);
      out += " support=";
      out += to_string(view.support);
      out += " publisher=";
      out += view.source.publisher.id.str();
      out += " boot=";
      out += view.source.publisher.boot.str();
      out += " evidence-generation=";
      out += view.source.evidence_generation.str();
      out += view.winner ? " [selected]" : " [not-selected]";
      if (is_stale(view.staleness)) {
        out += " stale=";
        out += describe_staleness(view.staleness);
      }
    }
    return out;
  }

  void touch_locked() { ++state_revision; }

  /// Enforces bounded retention. Eviction is deterministic: the oldest record
  /// that has already been superseded inside its own source slot is removed
  /// first, and the newest record of every slot is never evicted. A record that
  /// cannot be evicted without losing current evidence is kept, and ingestion
  /// is refused instead (explicit backpressure).
  void evict_if_needed_locked() {
    const std::size_t per_key_cap = limits.max_evidence_per_capability * 8u;
    for (auto& entry : keys) {
      KeyState& key = entry.second;
      while (key.records.size() > per_key_cap) {
        std::size_t victim = key.records.size();
        for (std::size_t i = 0; i < key.records.size(); ++i) {
          bool superseded = false;
          for (std::size_t j = 0; j < key.records.size(); ++j) {
            if (i == j) continue;
            if (key.records[j].slot == key.records[i].slot && key.records[j].serial > key.records[i].serial) {
              superseded = true;
              break;
            }
          }
          if (!superseded) continue;
          if (victim == key.records.size() ||
              key.records[i].serial < key.records[victim].serial) {
            victim = i;
          }
        }
        if (victim == key.records.size()) break;
        key.records.erase(key.records.begin() + static_cast<std::ptrdiff_t>(victim));
        --evidence_count;
      }
    }
    std::size_t guard = global_order.size() + 1;
    while (evidence_count >= limits.max_evidence_history && guard-- > 0) {
      if (global_order.empty()) break;
      const GlobalEntry front = global_order.front();
      global_order.pop_front();
      const auto key_it = keys.find(front.key);
      if (key_it == keys.end()) continue;
      KeyState& key = key_it->second;
      std::size_t index = key.records.size();
      for (std::size_t i = 0; i < key.records.size(); ++i) {
        if (key.records[i].serial == front.serial) {
          index = i;
          break;
        }
      }
      if (index == key.records.size()) continue;
      bool superseded = false;
      for (std::size_t j = 0; j < key.records.size(); ++j) {
        if (j == index) continue;
        if (key.records[j].slot == key.records[index].slot &&
            key.records[j].serial > key.records[index].serial) {
          superseded = true;
          break;
        }
      }
      if (!superseded) continue;
      key.records.erase(key.records.begin() + static_cast<std::ptrdiff_t>(index));
      --evidence_count;
    }
    while (!global_order.empty() && global_order.size() > limits.max_evidence_history * 4u) {
      global_order.pop_front();
    }
  }

  const StoredRecord* current_winner_record(const DeviceState& device, const CapabilityId& capability) const {
    const auto key_it = keys.find(KeyId{device.identity.id, capability});
    if (key_it == keys.end()) return nullptr;
    const KeyState& key = key_it->second;
    const StoredRecord* winner = nullptr;
    for (const StoredRecord& stored : key.records) {
      if (stored.persistent != StalenessFlag::None) continue;
      const StalenessFlag flags = compute_staleness_locked(stored, device, device.generation, true);
      if (flags != StalenessFlag::None) continue;
      if (winner == nullptr) {
        winner = &stored;
        continue;
      }
      const int left = stored.record.source.authority();
      const int right = winner->record.source.authority();
      if (left > right) {
        winner = &stored;
      } else if (left == right) {
        if (stored.record.source.publisher.id < winner->record.source.publisher.id ||
            (stored.record.source.publisher.id == winner->record.source.publisher.id &&
             stored.record.source.observation_sequence > winner->record.source.observation_sequence)) {
          winner = &stored;
        }
      }
    }
    return winner;
  }

  ApplyResult register_publisher_locked(const PublisherIdentity& identity, const std::string& adapter) {
    ApplyResult result;
    if (!identity.id.valid()) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "publisher identity is not canonical";
      return result;
    }
    if (adapter.empty() || adapter.size() > limits.max_metadata_bytes) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "publisher adapter must be a bounded canonical name";
      return result;
    }
    auto it = publishers.find(identity.id);
    if (it == publishers.end()) {
      if (publishers.size() >= limits.max_publishers) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "publisher limit reached";
        return result;
      }
      if (identity.boot.valid() && identity.boot.value() > 1) {
        result.code = ApplyCode::RejectedStaleBoot;
        result.detail = "first registration of a publisher must request boot assignment (boot 0 or 1)";
        return result;
      }
      PublisherStateInternal state;
      state.record.adapter = adapter;
      state.record.registered_at_utc = utc_now_iso8601();
      state.record.identity.id = identity.id;
      state.record.identity.boot = PublisherBootId::first();
      state.record.identity.epoch = epoch;
      state.record.state = PublisherState::Registered;
      state.boot_history = 1;
      publishers.emplace(identity.id, std::move(state));
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.assigned_publisher = publishers[identity.id].record.identity;
      result.detail = "publisher registered with boot 1 in epoch " + epoch.str();
      ++usage.publishers;
      touch_locked();
      return result;
    }

    PublisherStateInternal& state = it->second;
    if (state.publisher_fenced) {
      result.code = ApplyCode::RejectedFencedPublisher;
      result.detail = "publisher is permanently fenced: " + state.record.reason;
      return result;
    }
    if (!identity.boot.valid()) {
      // Fresh process: assign a new boot identity and permanently retire every
      // earlier boot of this publisher.
      if (state.record.identity.boot.valid()) {
        state.fenced_boots.insert(state.record.identity.boot);
      }
      const PublisherBootId next_boot{state.record.identity.boot.valid()
                                          ? state.record.identity.boot.value() + 1
                                          : 1};
      state.record.identity.boot = next_boot;
      state.record.identity.epoch = epoch;
      state.record.state = PublisherState::Registered;
      state.record.adapter = adapter;
      state.record.reason.clear();
      state.record.registered_at_utc = utc_now_iso8601();
      ++state.boot_history;
      state.record.boot_history = state.boot_history;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.assigned_publisher = state.record.identity;
      result.detail = "publisher re-registered with fresh boot " + next_boot.str() + "; prior boots fenced";
      touch_locked();
      return result;
    }
    if (state.fenced_boots.count(identity.boot) != 0) {
      result.code = ApplyCode::RejectedFencedPublisher;
      result.detail = "publisher boot " + identity.boot.str() + " is fenced and never regains authority";
      return result;
    }
    if (state.record.identity.boot != identity.boot) {
      result.code = ApplyCode::RejectedStaleBoot;
      result.detail = "publisher boot " + identity.boot.str() + " is not the registered boot " +
                      state.record.identity.boot.str();
      return result;
    }
    state.record.identity.epoch = epoch;
    state.record.state = PublisherState::Registered;
    state.record.adapter = adapter;
    state.record.reason.clear();
    result.code = ApplyCode::AcceptedIdempotent;
    result.assigned_publisher = state.record.identity;
    result.detail = "publisher boot re-affirmed";
    return result;
  }

  /// Ingestion entry point. The publication is taken by value because the
  /// registry binds two things that a publisher is not trusted to state: the
  /// provenance publisher identity (always the authenticated envelope sender,
  /// so a record can never be attributed to another publisher) and the
  /// versioned schema a capability is bound to.
  ApplyResult apply_locked(Publication publication) {
    ApplyResult result;
    if (shutting_down) {
      result.code = ApplyCode::RejectedShuttingDown;
      result.detail = "registry is shutting down and no longer accepts evidence";
      return result;
    }
    publication.capability.source.publisher = publication.publisher;
    publication.quirk.source.publisher = publication.publisher;
    publication.compatibility.source.publisher = publication.publisher;
    if ((publication.kind == PublicationKind::PublishCapability ||
         publication.kind == PublicationKind::WithdrawCapability) &&
        !publication.capability.schema.valid()) {
      const auto schema_it = capability_schemas.find(publication.capability.capability);
      if (schema_it == capability_schemas.end()) {
        result.code = ApplyCode::RejectedUnknownSchema;
        result.detail = "capability " + publication.capability.capability.str() +
                        " is not bound to any known schema";
        ++usage.rejected_publications;
        return result;
      }
      publication.capability.schema = schema_it->second;
    }
    if (publication.kind == PublicationKind::PublishCapability ||
        publication.kind == PublicationKind::WithdrawCapability) {
      // Values are canonicalized before validation, so a publisher may present a
      // set in any order while the registry stores and compares one canonical
      // form.
      const Status normalized = normalize_value(publication.capability.value, limits);
      if (!normalized.ok()) {
        result.code = ApplyCode::RejectedValidation;
        result.detail = normalized.describe();
        ++usage.rejected_publications;
        return result;
      }
    }
    const Status validation = validate_publication(publication, limits);
    if (!validation.ok()) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = validation.describe();
      ++usage.rejected_publications;
      return result;
    }
    if (publication.kind == PublicationKind::RegisterCapabilitySchema) {
      const Status status = register_capability_schema_locked(publication.schema);
      if (!status.ok()) {
        result.code = status.code() == ErrorCode::AlreadyExists ? ApplyCode::RejectedDuplicateConflict
                                                                : ApplyCode::RejectedValidation;
        result.detail = status.describe();
        ++usage.rejected_publications;
        return result;
      }
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.detail = "capability schema " + publication.schema.id.str() + " registered";
      touch_locked();
      return result;
    }
    if (publication.kind == PublicationKind::RegisterPublisher) {
      ApplyResult registration = register_publisher_locked(publication.publisher, publication.reason);
      if (registration.accepted()) {
        ++usage.accepted_publications;
      } else {
        ++usage.rejected_publications;
      }
      return registration;
    }

    auto publisher_it = publishers.find(publication.publisher.id);
    if (publisher_it == publishers.end()) {
      result.code = ApplyCode::RejectedUnknownPublisher;
      result.detail = "publisher " + publication.publisher.id.str() + " is not registered";
      ++usage.rejected_publications;
      return result;
    }
    PublisherStateInternal& publisher = publisher_it->second;
    if (publisher.publisher_fenced) {
      result.code = ApplyCode::RejectedFencedPublisher;
      result.detail = "publisher is permanently fenced: " + publisher.record.reason;
      ++usage.rejected_publications;
      return result;
    }
    if (publisher.fenced_boots.count(publication.publisher.boot) != 0) {
      result.code = ApplyCode::RejectedFencedPublisher;
      result.detail = "publisher boot " + publication.publisher.boot.str() + " is fenced; replay is refused";
      ++usage.rejected_publications;
      return result;
    }
    if (publisher.record.identity.boot != publication.publisher.boot) {
      result.code = ApplyCode::RejectedStaleBoot;
      result.detail = "publication carries boot " + publication.publisher.boot.str() +
                      " but the registered boot is " + publisher.record.identity.boot.str();
      ++usage.rejected_publications;
      return result;
    }
    if (publication.publisher.epoch != epoch) {
      result.code = ApplyCode::RejectedStaleEpoch;
      result.detail = "publication carries epoch " + publication.publisher.epoch.str() +
                      " but the coordinator epoch is " + epoch.str();
      ++usage.rejected_publications;
      return result;
    }

    const PublisherBootId boot = publication.publisher.boot;
    PublisherWatermark& watermark = publisher.watermarks[boot];
    watermark.publisher = publication.publisher.id;
    watermark.boot = boot;
    if (watermark.state == PublisherState::Unknown) {
      watermark.state = PublisherState::Registered;
      watermark.adapter = publisher.record.adapter;
    }

    if (publication.sequence < watermark.max_sequence) {
      result.code = ApplyCode::RejectedStaleSequence;
      result.detail = "sequence " + std::to_string(publication.sequence) + " is behind the accepted watermark " +
                      std::to_string(watermark.max_sequence);
      ++publisher.record.rejected;
      watermark.rejected = publisher.record.rejected;
      ++usage.rejected_publications;
      return result;
    }
    if (publication.sequence == watermark.max_sequence) {
      const auto fingerprint_it = publisher.sequence_fingerprints.find(publication.sequence);
      if (fingerprint_it == publisher.sequence_fingerprints.end()) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "sequence " + std::to_string(publication.sequence) +
                        " was already accepted; no mutation applied";
        ++usage.idempotent_publications;
        return result;
      }
      if (fingerprint_it->second == std::hash<std::string>{}(fingerprint_publication(publication))) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "identical duplicate of sequence " + std::to_string(publication.sequence);
        ++usage.idempotent_publications;
        return result;
      }
      result.code = ApplyCode::RejectedDuplicateConflict;
      result.detail = "sequence " + std::to_string(publication.sequence) +
                      " was already accepted with different content";
      ++publisher.record.rejected;
      watermark.rejected = publisher.record.rejected;
      ++usage.rejected_publications;
      return result;
    }
    if (publication.evidence_generation < watermark.max_evidence_generation) {
      result.code = ApplyCode::RejectedGenerationRegression;
      result.detail = "evidence generation " + publication.evidence_generation.str() +
                      " regresses behind the accepted watermark " +
                      watermark.max_evidence_generation.str();
      ++publisher.record.rejected;
      watermark.rejected = publisher.record.rejected;
      ++usage.rejected_publications;
      return result;
    }

    switch (publication.kind) {
      case PublicationKind::RegisterDevice:
      case PublicationKind::PublishDeviceGeneration:
        result = apply_device_locked(publication);
        break;
      case PublicationKind::PublishCapability:
      case PublicationKind::WithdrawCapability:
        result = apply_capability_locked(publication);
        break;
      case PublicationKind::PublishQuirk:
      case PublicationKind::WithdrawQuirk:
        result = apply_quirk_locked(publication);
        break;
      case PublicationKind::PublishCompatibility:
      case PublicationKind::WithdrawCompatibility:
        result = apply_compatibility_locked(publication);
        break;
      case PublicationKind::Heartbeat: {
        result.code = ApplyCode::Accepted;
        result.mutated = false;
        result.detail = "heartbeat recorded";
        break;
      }
      default:
        result.code = ApplyCode::RejectedValidation;
        result.detail = "unsupported publication kind";
        break;
    }

    if (result.accepted()) {
      if (publication.sequence > watermark.max_sequence + 1 && watermark.max_sequence != 0) {
        result.detail += "; sequence gap detected: " + std::to_string(watermark.max_sequence) + " -> " +
                         std::to_string(publication.sequence);
      }
      watermark.max_sequence = publication.sequence;
      if (publication.evidence_generation > watermark.max_evidence_generation) {
        watermark.max_evidence_generation = publication.evidence_generation;
      }
      publisher.sequence_fingerprints[publication.sequence] =
          std::hash<std::string>{}(fingerprint_publication(publication));
      while (publisher.sequence_fingerprints.size() > 4096) {
        publisher.sequence_fingerprints.erase(publisher.sequence_fingerprints.begin());
      }
      publisher.record.last_sequence = publication.sequence;
      publisher.record.max_evidence_generation = watermark.max_evidence_generation;
      publisher.record.accepted = publisher.record.accepted + 1;
      watermark.accepted = publisher.record.accepted;
      watermark.max_sequence = publication.sequence;
      watermark.max_evidence_generation = publisher.record.max_evidence_generation;
      ++usage.accepted_publications;
      if (result.code == ApplyCode::AcceptedIdempotent) ++usage.idempotent_publications;
      if (result.mutated) touch_locked();
    } else {
      ++publisher.record.rejected;
      watermark.rejected = publisher.record.rejected;
      ++usage.rejected_publications;
    }
    return result;
  }

  ApplyResult apply_device_locked(const Publication& publication) {
    ApplyResult result;
    const DeviceRegistration& registration = publication.device;
    const DeviceIdentity& identity = registration.identity;
    DeviceState* existing = find_device_locked(identity.id);
    const bool explicit_generation = publication.kind == PublicationKind::PublishDeviceGeneration;

    if (existing == nullptr) {
      if (devices.size() >= limits.max_devices) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "device limit reached";
        return result;
      }
      if (identity.hardware.family.valid() && families.size() >= limits.max_families &&
          families.find(identity.hardware.family) == families.end()) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "hardware family limit reached";
        return result;
      }
      DeviceState state;
      state.identity = identity;
      normalize_hardware_identity(state.identity.hardware);
      state.software = registration.software;
      state.generation = DeviceGeneration::first();
      state.boot = DeviceBootId::first();
      state.platform_generation = PlatformGeneration::first();
      state.software.driver_generation =
          registration.software.driver.id.valid() ? DriverGeneration::first() : DriverGeneration{};
      state.software.firmware_generation =
          registration.software.firmware.id.valid() ? FirmwareGeneration::first() : FirmwareGeneration{};
      state.software.runtime_generation =
          registration.software.runtime.id.valid() ? RuntimeGeneration::first() : RuntimeGeneration{};
      devices.emplace(identity.id, state);
      if (identity.hardware.family.valid()) families.insert(identity.hardware.family);
      if (identity.hardware.model.valid()) models.insert(identity.hardware.model);
      if (identity.hardware.architecture.valid()) architectures.insert(identity.hardware.architecture);
      register_platform_locked(registration.platform, true);
      ++usage.devices;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.device_generation = state.generation;
      result.device_boot = state.boot;
      result.detail = "device registered at generation 1, boot 1";
      return result;
    }

    bool boundary = false;
    if (existing->identity.incarnation != identity.incarnation) {
      boundary = true;
      result.detail = "device incarnation changed (" + existing->identity.incarnation.str() + " -> " +
                      identity.incarnation.str() + "); new capability generation opened";
    } else if (registration.device_boot_observed || explicit_generation) {
      boundary = true;
      result.detail = "device boot observed; new capability generation opened";
    }

    existing->identity.display_name = identity.display_name;
    existing->identity.hardware = identity.hardware;
    normalize_hardware_identity(existing->identity.hardware);
    existing->identity.serial_number = identity.serial_number;
    existing->identity.instance_locator = identity.instance_locator;
    if (existing->identity.platform != identity.platform) {
      existing->platform_generation = existing->platform_generation.next();
      existing->identity.platform = identity.platform;
      result.detail += "; platform identity changed, platform generation advanced";
    }
    register_platform_locked(registration.platform, false);

    const DeviceSoftwareState previous = existing->software;
    // An observation that does not identify an axis must not erase what is
    // already known about it: a NIC adapter that cannot read device firmware
    // does not get to forget the firmware another adapter reported.
    DeviceSoftwareState merged = registration.software;
    if (!merged.firmware.id.valid()) merged.firmware = previous.firmware;
    if (!merged.driver.id.valid()) merged.driver = previous.driver;
    if (!merged.runtime.id.valid()) merged.runtime = previous.runtime;
    existing->software = merged;
    // Driver, firmware and runtime generations are monotonic counters. They
    // advance whenever the identified software state changes, never regress,
    // and are the axis on which live capability evidence goes stale.
    const auto advance_firmware_generation = [&]() {
      if (!existing->software.firmware.id.valid()) {
        existing->software.firmware_generation = previous.firmware_generation;
        return;
      }
      if (!previous.firmware.id.valid() || !(previous.firmware == existing->software.firmware)) {
        existing->software.firmware_generation = previous.firmware_generation.valid()
                                                     ? previous.firmware_generation.next()
                                                     : FirmwareGeneration::first();
        result.detail += "; firmware generation advanced to " + existing->software.firmware_generation.str();
        return;
      }
      existing->software.firmware_generation = previous.firmware_generation.valid()
                                                   ? previous.firmware_generation
                                                   : FirmwareGeneration::first();
    };
    const auto advance_driver_generation = [&]() {
      if (!existing->software.driver.id.valid()) {
        existing->software.driver_generation = previous.driver_generation;
        return;
      }
      if (!previous.driver.id.valid() || !(previous.driver == existing->software.driver)) {
        existing->software.driver_generation = previous.driver_generation.valid()
                                                   ? previous.driver_generation.next()
                                                   : DriverGeneration::first();
        result.detail += "; driver generation advanced to " + existing->software.driver_generation.str();
        return;
      }
      existing->software.driver_generation =
          previous.driver_generation.valid() ? previous.driver_generation : DriverGeneration::first();
    };
    const auto advance_runtime_generation = [&]() {
      if (!existing->software.runtime.id.valid()) {
        existing->software.runtime_generation = previous.runtime_generation;
        return;
      }
      if (!previous.runtime.id.valid() || !(previous.runtime == existing->software.runtime)) {
        existing->software.runtime_generation = previous.runtime_generation.valid()
                                                    ? previous.runtime_generation.next()
                                                    : RuntimeGeneration::first();
        result.detail += "; runtime generation advanced to " + existing->software.runtime_generation.str();
        return;
      }
      existing->software.runtime_generation =
          previous.runtime_generation.valid() ? previous.runtime_generation : RuntimeGeneration::first();
    };
    advance_firmware_generation();
    advance_driver_generation();
    advance_runtime_generation();
    if (boundary) {
      existing->generation = existing->generation.next();
      existing->boot = existing->boot.next();
    }
    result.code = ApplyCode::Accepted;
    result.mutated = true;
    result.device_generation = existing->generation;
    result.device_boot = existing->boot;
    if (result.detail.empty()) {
      result.detail = "device registration refreshed";
    } else {
      result.detail = "device generation " + existing->generation.str() + "; " + result.detail;
    }
    return result;
  }

  void register_platform_locked(const PlatformIdentity& platform, bool create_generation) {
    if (!platform.id.valid()) return;
    auto it = platforms.find(platform.id);
    if (it == platforms.end()) {
      if (platforms.size() >= limits.max_devices) return;
      platforms.emplace(platform.id, platform);
      ++usage.platforms;
      return;
    }
    (void)create_generation;
    it->second.display_name = platform.display_name;
    it->second.vendor = platform.vendor;
    it->second.model = platform.model;
    it->second.architecture = platform.architecture;
    it->second.native_identifiers = platform.native_identifiers;
    std::sort(it->second.native_identifiers.begin(), it->second.native_identifiers.end());
  }

  ApplyResult apply_capability_locked(const Publication& publication) {
    ApplyResult result;
    CapabilityRecord record = publication.capability;
    const CapabilitySchema* schema = find_schema_locked(record.schema);
    if (schema == nullptr) {
      result.code = ApplyCode::RejectedUnknownSchema;
      result.detail = "unknown capability schema " + record.schema.str();
      return result;
    }
    if (schema->capability != record.capability) {
      result.code = ApplyCode::RejectedUnknownSchema;
      result.detail = "schema " + record.schema.str() + " describes capability " +
                      schema->capability.str() + ", not " + record.capability.str();
      return result;
    }
    if (!record.value.empty() && record.value.kind() != schema->value_kind) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = std::string("value kind ") + to_string(record.value.kind()) +
                      " does not match the schema kind " + to_string(schema->value_kind);
      return result;
    }
    if (record.value.empty() && !asserts_capability_absent(record.support) &&
        record.support != SupportState::Unknown && record.support != SupportState::RevalidationRequired) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "schema requires a typed value for support state " + std::string(to_string(record.support));
      return result;
    }
    const Status value_status = normalize_value(record.value, limits);
    if (!value_status.ok()) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = value_status.describe();
      return result;
    }

    DeviceState* device = find_device_locked(record.device);
    if (device == nullptr) {
      result.code = ApplyCode::RejectedUnknownDevice;
      result.detail = "device " + record.device.str() + " is not registered";
      return result;
    }
    if (record.device_generation != device->generation) {
      result.code = ApplyCode::RejectedStaleDeviceGeneration;
      result.detail = "capability evidence targets device generation " + record.device_generation.str() +
                      " but the current generation is " + device->generation.str();
      return result;
    }
    if (record.device_boot != device->boot) {
      result.code = ApplyCode::RejectedStaleDeviceGeneration;
      result.detail = "capability evidence targets device boot " + record.device_boot.str() +
                      " but the current boot is " + device->boot.str();
      return result;
    }

    const KeyId key_id{record.device, record.capability};
    auto key_it = keys.find(key_id);
    if (key_it == keys.end()) {
      if (device_keys[record.device].size() >= limits.max_capabilities_per_device) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "capability key limit reached for device " + record.device.str();
        return result;
      }
      if (keys.size() >= limits.max_evidence_history) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "capability key limit reached";
        return result;
      }
      KeyState state;
      state.schema = record.schema;
      key_it = keys.emplace(key_id, std::move(state)).first;
      device_keys[record.device].insert(record.capability);
      ++usage.capability_keys;
    }
    KeyState& key = key_it->second;
    if (key.schema != record.schema) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "capability " + record.capability.str() + " is already bound to schema " +
                      key.schema.str() + " and may not be reinterpreted by " + record.schema.str();
      return result;
    }
    if (record.capability_generation.valid() &&
        key.generation.valid() && record.capability_generation <= key.generation) {
      result.code = ApplyCode::RejectedStaleCapabilityGeneration;
      result.detail = "capability generation " + record.capability_generation.str() +
                      " does not advance the current generation " + key.generation.str();
      return result;
    }
    if (record.capability_generation.valid() && !key.generation.valid() &&
        record.capability_generation.value() > 1) {
      result.code = ApplyCode::RejectedStaleCapabilityGeneration;
      result.detail = "first publication for a capability key must not skip generations";
      return result;
    }

    record.firmware_generation = device->software.firmware_generation;
    record.driver_generation = device->software.driver_generation;
    record.runtime_generation = device->software.runtime_generation;
    record.platform_generation = device->platform_generation;
    record.driver_id = device->software.driver.id;
    record.firmware_id = device->software.firmware.id;
    record.runtime_id = device->software.runtime.id;

    // A source slot is one publisher boot observing through one source class.
    // Native references and precision are provenance metadata, not identity:
    // re-observing through the same source supersedes that source's previous
    // claim, which is what makes withdrawal retract it.
    const std::string slot = record.source.publisher.id.str() + "#" + record.source.publisher.boot.str() + "#" +
                             to_string(record.source.source_class);
    const StoredRecord* slot_latest = nullptr;
    for (const StoredRecord& stored : key.records) {
      if (stored.slot != slot) continue;
      if (slot_latest == nullptr || stored.serial > slot_latest->serial) slot_latest = &stored;
    }
    if (slot_latest != nullptr && !record.withdrawn && !slot_latest->record.withdrawn &&
        capability_content_equal(slot_latest->record, record) &&
        slot_latest->record.device_generation == record.device_generation &&
        slot_latest->record.driver_generation == record.driver_generation &&
        slot_latest->record.firmware_generation == record.firmware_generation &&
        slot_latest->record.runtime_generation == record.runtime_generation) {
      result.code = ApplyCode::AcceptedIdempotent;
      result.detail = "identical evidence already recorded for this source slot";
      return result;
    }

    const StoredRecord* winner = current_winner_record(*device, record.capability);
    if (winner != nullptr && capability_content_equal(winner->record, record) &&
        winner->record.device_generation == record.device_generation) {
      // Corroborating evidence for the same truth joins the winning record's
      // generation rather than opening a new one. Reusing the key counter
      // instead would let two different claims share one generation number.
      record.capability_generation = winner->record.capability_generation.valid()
                                         ? winner->record.capability_generation
                                         : CapabilityGeneration::first();
    } else {
      record.capability_generation =
          key.generation.valid() ? key.generation.next() : CapabilityGeneration::first();
    }
    if (!key.generation.valid() || record.capability_generation > key.generation) {
      key.generation = record.capability_generation;
    }
    key.has_evidence = true;

    if (evidence_count >= limits.max_evidence_history) {
      evict_if_needed_locked();
      if (evidence_count >= limits.max_evidence_history) {
        result.code = ApplyCode::RejectedBackpressure;
        result.detail = "evidence history is at its bound; no evictable record remains";
        return result;
      }
    }

    for (StoredRecord& stored : key.records) {
      if (stored.slot == slot) stored.persistent |= StalenessFlag::SupersededByNewerEvidence;
    }
    StoredRecord stored;
    stored.record = record;
    stored.slot = slot;
    stored.serial = next_serial++;
    key.records.push_back(std::move(stored));
    global_order.push_back(GlobalEntry{key_id, key.records.back().serial});
    ++evidence_count;
    evict_if_needed_locked();

    result.code = ApplyCode::Accepted;
    result.mutated = true;
    result.detail = "capability " + record.capability.str() + " accepted at capability generation " +
                    record.capability_generation.str();
    return result;
  }

  ApplyResult apply_quirk_locked(const Publication& publication) {
    ApplyResult result;
    QuirkRecord quirk = publication.quirk;
    for (const CapabilityId& capability : quirk.impacted_capabilities) {
      bool known = false;
      for (const auto& entry : schemas) {
        if (entry.second.capability == capability) {
          known = true;
          break;
        }
      }
      if (!known) {
        result.code = ApplyCode::RejectedUnknownSchema;
        result.detail = "quirk impacts unknown capability " + capability.str();
        return result;
      }
    }
    auto it = quirks.find(quirk.id);
    if (publication.kind == PublicationKind::WithdrawQuirk) {
      if (it == quirks.end()) {
        result.code = ApplyCode::RejectedUnknownDevice;
        result.detail = "cannot withdraw unknown quirk " + quirk.id.str();
        return result;
      }
      if (it->second.withdrawn) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "quirk already withdrawn";
        return result;
      }
      it->second.withdrawn = true;
      it->second.source = quirk.source;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.detail = "quirk " + quirk.id.str() + " withdrawn";
      return result;
    }
    if (!quirk.quirk_generation.valid()) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "quirk requires a generation";
      return result;
    }
    if (it == quirks.end()) {
      if (quirks.size() >= limits.max_quirks) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "quirk limit reached";
        return result;
      }
      if (quirk.quirk_generation.value() > 1) {
        result.code = ApplyCode::RejectedStaleCapabilityGeneration;
        result.detail = "first publication of a quirk must use generation 1";
        return result;
      }
      quirks.emplace(quirk.id, quirk);
      ++usage.quirks;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.detail = "quirk " + quirk.id.str() + " published at generation 1";
      return result;
    }
    const QuirkRecord& existing = it->second;
    if (quirk.quirk_generation < existing.quirk_generation) {
      result.code = ApplyCode::RejectedGenerationRegression;
      result.detail = "quirk generation " + quirk.quirk_generation.str() + " regresses behind " +
                      existing.quirk_generation.str();
      return result;
    }
    if (quirk.quirk_generation == existing.quirk_generation) {
      if (existing.title == quirk.title && existing.severity == quirk.severity &&
          existing.category == quirk.category &&
          existing.impacted_capabilities == quirk.impacted_capabilities) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "identical quirk publication";
        return result;
      }
      result.code = ApplyCode::RejectedDuplicateConflict;
      result.detail = "quirk generation " + quirk.quirk_generation.str() +
                      " was already published with different content";
      return result;
    }
    it->second = quirk;
    result.code = ApplyCode::Accepted;
    result.mutated = true;
    result.detail = "quirk " + quirk.id.str() + " superseded at generation " + quirk.quirk_generation.str();
    return result;
  }

  bool reference_resolves_locked(const HardwareReference& reference) const {
    switch (reference.kind) {
      case HardwareReference::Kind::Device:
        return devices.find(DeviceId::parse(reference.token)) != devices.end();
      case HardwareReference::Kind::Platform:
        return platforms.find(PlatformId::parse(reference.token)) != platforms.end();
      case HardwareReference::Kind::HardwareFamily:
        return families.find(HardwareFamilyId::parse(reference.token)) != families.end();
      case HardwareReference::Kind::HardwareModel:
        return models.find(HardwareModelId::parse(reference.token)) != models.end();
      case HardwareReference::Kind::Architecture:
        return architectures.find(ArchitectureId::parse(reference.token)) != architectures.end();
      default:
        return true;
    }
  }

  ApplyResult apply_compatibility_locked(const Publication& publication) {
    ApplyResult result;
    CompatibilityFact fact = publication.compatibility;
    if (!reference_resolves_locked(fact.lhs)) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "compatibility reference " + fact.lhs.token + " does not resolve to a known subject";
      return result;
    }
    if (!reference_resolves_locked(fact.rhs)) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "compatibility reference " + fact.rhs.token + " does not resolve to a known subject";
      return result;
    }
    auto it = compatibility.find(fact.id);
    if (publication.kind == PublicationKind::WithdrawCompatibility) {
      if (it == compatibility.end()) {
        result.code = ApplyCode::RejectedUnknownDevice;
        result.detail = "cannot withdraw unknown compatibility fact " + fact.id.str();
        return result;
      }
      if (it->second.withdrawn) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "compatibility fact already withdrawn";
        return result;
      }
      it->second.withdrawn = true;
      it->second.source = fact.source;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.detail = "compatibility fact " + fact.id.str() + " withdrawn";
      return result;
    }
    if (!fact.compatibility_generation.valid()) {
      result.code = ApplyCode::RejectedValidation;
      result.detail = "compatibility fact requires a generation";
      return result;
    }
    if (it == compatibility.end()) {
      if (compatibility.size() >= limits.max_compatibility_facts) {
        result.code = ApplyCode::RejectedLimit;
        result.detail = "compatibility fact limit reached";
        return result;
      }
      if (fact.compatibility_generation.value() > 1) {
        result.code = ApplyCode::RejectedStaleCapabilityGeneration;
        result.detail = "first publication of a compatibility fact must use generation 1";
        return result;
      }
      compatibility.emplace(fact.id, fact);
      ++usage.compatibility_facts;
      result.code = ApplyCode::Accepted;
      result.mutated = true;
      result.detail = "compatibility fact " + fact.id.str() + " published at generation 1";
      return result;
    }
    const CompatibilityFact& existing = it->second;
    if (fact.compatibility_generation < existing.compatibility_generation) {
      result.code = ApplyCode::RejectedGenerationRegression;
      result.detail = "compatibility generation " + fact.compatibility_generation.str() + " regresses behind " +
                      existing.compatibility_generation.str();
      return result;
    }
    if (fact.compatibility_generation == existing.compatibility_generation) {
      if (existing.outcome == fact.outcome && existing.lhs == fact.lhs && existing.rhs == fact.rhs &&
          existing.reason == fact.reason) {
        result.code = ApplyCode::AcceptedIdempotent;
        result.detail = "identical compatibility publication";
        return result;
      }
      result.code = ApplyCode::RejectedDuplicateConflict;
      result.detail = "compatibility generation " + fact.compatibility_generation.str() +
                      " was already published with different content";
      return result;
    }
    it->second = fact;
    result.code = ApplyCode::Accepted;
    result.mutated = true;
    result.detail = "compatibility fact " + fact.id.str() + " superseded at generation " +
                    fact.compatibility_generation.str();
    return result;
  }

  Status register_capability_schema_locked(const CapabilitySchema& schema) {
    const Status validation = validate_capability_schema(schema, limits);
    if (!validation.ok()) return validation;
    const auto existing = schemas.find(schema.id);
    if (existing != schemas.end()) {
      const CapabilitySchema& current = existing->second;
      if (current.capability == schema.capability && current.value_kind == schema.value_kind &&
          current.domain == schema.domain && current.canonical_name == schema.canonical_name &&
          current.unit == schema.unit) {
        return Status::failure(ErrorCode::AlreadyExists, "identical schema already registered");
      }
      return Status::failure(ErrorCode::AlreadyExists,
                             "schema " + schema.id.str() + " is already registered with different content");
    }
    const auto capability_it = capability_schemas.find(schema.capability);
    if (capability_it != capability_schemas.end()) {
      return Status::failure(ErrorCode::AlreadyExists,
                             "capability " + schema.capability.str() + " is already bound to schema " +
                                 capability_it->second.str());
    }
    if (schemas.size() >= limits.max_families * 16u) {
      return Status::failure(ErrorCode::LimitExceeded, "capability schema limit reached");
    }
    capability_schemas.emplace(schema.capability, schema.id);
    schemas.emplace(schema.id, schema);
    ++usage.schemas;
    return Status::success();
  }

  SubjectSnapshot build_subject_locked(const DeviceState& device, const EnvironmentContext& environment) const {
    SubjectSnapshot subject;
    subject.identity = device.identity;
    subject.software = device.software;
    subject.generation = device.generation;
    subject.boot = device.boot;
    subject.platform_generation = device.platform_generation;
    std::size_t real = 0;
    std::size_t synthetic = 0;
    std::size_t unsupported = 0;
    const auto owned = device_keys.find(device.identity.id);
    if (owned == device_keys.end()) {
      subject.classification = TruthClass::Unknown;
      return subject;
    }
    for (const CapabilityId& capability_id : owned->second) {
      const ResolvedCapability resolved =
          resolve_locked(device, capability_id, DeviceGeneration{}, environment);
      ++subject.capability_count;
      if (resolved.current && !is_stale(resolved.staleness)) {
        ++subject.current_count;
      } else {
        ++subject.stale_count;
      }
      if (resolved.effective_support == EffectiveSupport::Unknown ||
          resolved.declared_support == SupportState::Unknown) {
        ++subject.unknown_count;
      }
      if (resolved.effective_support == EffectiveSupport::Unsupported ||
          resolved.declared_support == SupportState::Unsupported) {
        ++subject.unsupported_count;
      }
      for (const QuirkApplication& application : resolved.quirks) {
        if (application.result == QuirkApplicabilityResult::Applies) ++subject.applicable_quirk_count;
      }
      switch (resolved.truth) {
        case TruthClass::Real: ++real; break;
        case TruthClass::Synthetic: ++synthetic; break;
        case TruthClass::Unsupported: ++unsupported; break;
        case TruthClass::Unknown: break;
      }
    }
    if (subject.capability_count == 0) {
      subject.classification = TruthClass::Unknown;
    } else if (real != 0) {
      subject.classification = TruthClass::Real;
    } else if (synthetic != 0) {
      subject.classification = TruthClass::Synthetic;
    } else if (unsupported != 0) {
      subject.classification = TruthClass::Unsupported;
    } else {
      subject.classification = TruthClass::Unknown;
    }
    return subject;
  }
};

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

CapabilityRegistry::CapabilityRegistry(RegistryLimits limits) : impl_(std::make_unique<Impl>(limits)) {
  for (const CapabilitySchema& schema : builtin_capability_schemas()) {
    impl_->schemas.emplace(schema.id, schema);
    impl_->capability_schemas.emplace(schema.capability, schema.id);
  }
  impl_->usage.schemas = impl_->schemas.size();
  impl_->epoch = CoordinatorEpoch::first();
  impl_->snapshot_generation = SnapshotGeneration{};
}

CapabilityRegistry::~CapabilityRegistry() = default;

const RegistryLimits& CapabilityRegistry::limits() const noexcept { return impl_->limits; }

Status CapabilityRegistry::register_capability_schema(const CapabilitySchema& schema) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const Status status = impl_->register_capability_schema_locked(schema);
  if (status.ok()) impl_->touch_locked();
  return status;
}

bool CapabilityRegistry::has_capability_schema(const CapabilitySchemaId& schema) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->schemas.find(schema) != impl_->schemas.end();
}

Status CapabilityRegistry::find_capability_schema(const CapabilitySchemaId& schema, CapabilitySchema& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->schemas.find(schema);
  if (it == impl_->schemas.end()) {
    return Status::failure(ErrorCode::NotFound, "unknown capability schema " + schema.str());
  }
  out = it->second;
  return Status::success();
}

std::vector<CapabilitySchema> CapabilityRegistry::capability_schemas() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  std::vector<CapabilitySchema> out;
  out.reserve(impl_->schemas.size());
  for (const auto& entry : impl_->schemas) out.push_back(entry.second);
  return out;
}

ApplyResult CapabilityRegistry::register_publisher(const PublisherIdentity& identity, std::string adapter) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->shutting_down) {
    ApplyResult result;
    result.code = ApplyCode::RejectedShuttingDown;
    result.detail = "registry is shutting down";
    return result;
  }
  return impl_->register_publisher_locked(identity, adapter);
}

ApplyResult CapabilityRegistry::fence_publisher(const PublisherId& publisher, std::string reason) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  ApplyResult result;
  const auto it = impl_->publishers.find(publisher);
  if (it == impl_->publishers.end()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = "publisher " + publisher.str() + " is not registered";
    return result;
  }
  PublisherStateInternal& state = it->second;
  if (state.publisher_fenced) {
    result.code = ApplyCode::AcceptedIdempotent;
    result.detail = "publisher already fenced";
    return result;
  }
  state.publisher_fenced = true;
  state.record.state = PublisherState::Fenced;
  state.record.reason = reason.empty() ? "fenced by coordinator" : reason;
  if (state.record.identity.boot.valid()) state.fenced_boots.insert(state.record.identity.boot);
  for (auto& entry : state.watermarks) {
    entry.second.state = PublisherState::Fenced;
    entry.second.reason = state.record.reason;
  }
  result.code = ApplyCode::Accepted;
  result.mutated = true;
  result.detail = "publisher " + publisher.str() + " fenced: " + state.record.reason;
  impl_->touch_locked();
  return result;
}

ApplyResult CapabilityRegistry::retire_publisher_boot(const PublisherId& publisher, PublisherBootId boot,
                                                     std::string reason) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  ApplyResult result;
  const auto it = impl_->publishers.find(publisher);
  if (it == impl_->publishers.end()) {
    result.code = ApplyCode::RejectedUnknownPublisher;
    result.detail = "publisher " + publisher.str() + " is not registered";
    return result;
  }
  PublisherStateInternal& state = it->second;
  if (!boot.valid()) {
    result.code = ApplyCode::RejectedValidation;
    result.detail = "retiring a publisher boot requires the boot identity";
    return result;
  }
  if (state.fenced_boots.count(boot) != 0) {
    result.code = ApplyCode::AcceptedIdempotent;
    result.detail = "publisher boot " + boot.str() + " is already retired";
    return result;
  }
  state.fenced_boots.insert(boot);
  const auto watermark = state.watermarks.find(boot);
  if (watermark != state.watermarks.end()) {
    watermark->second.state = PublisherState::Dead;
    watermark->second.reason = reason;
  }
  if (state.record.identity.boot == boot) {
    state.record.state = PublisherState::Dead;
    state.record.reason = reason;
  }
  result.code = ApplyCode::Accepted;
  result.mutated = true;
  result.detail = "publisher boot " + boot.str() + " retired: " + reason;
  impl_->touch_locked();
  return result;
}

Status CapabilityRegistry::find_publisher_record(const PublisherId& publisher, PublisherRecord& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->publishers.find(publisher);
  if (it == impl_->publishers.end()) {
    return Status::failure(ErrorCode::NotFound, "publisher " + publisher.str() + " is not registered");
  }
  out = it->second.record;
  return Status::success();
}

Status CapabilityRegistry::list_publishers(const PublisherQuery& query, std::vector<PublisherRecord>& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  out.clear();
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  for (const auto& entry : impl_->publishers) {
    if (query.state.has_value() && entry.second.record.state != *query.state) continue;
    out.push_back(entry.second.record);
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::reset_publisher_liveness(std::string reason) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  for (auto& entry : impl_->publishers) {
    if (entry.second.record.state == PublisherState::Registered) {
      entry.second.record.state = PublisherState::Dead;
      entry.second.record.reason = reason;
      if (entry.second.record.identity.boot.valid()) {
        entry.second.fenced_boots.insert(entry.second.record.identity.boot);
        for (auto& watermark : entry.second.watermarks) {
          watermark.second.state = PublisherState::Dead;
          watermark.second.reason = reason;
        }
      }
    }
  }
  impl_->touch_locked();
  return Status::success();
}

ApplyResult CapabilityRegistry::apply(const Publication& publication) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->apply_locked(publication);
}

Status CapabilityRegistry::find_device(const DeviceId& device, SubjectSnapshot& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const DeviceState* state = impl_->find_device_locked(device);
  if (state == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + device.str() + " is not registered");
  }
  out = impl_->build_subject_locked(*state, EnvironmentContext{});
  return Status::success();
}

bool CapabilityRegistry::has_device(const DeviceId& device) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->devices.find(device) != impl_->devices.end();
}

Status CapabilityRegistry::find_platform(const PlatformId& platform, PlatformIdentity& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->platforms.find(platform);
  if (it == impl_->platforms.end()) {
    return Status::failure(ErrorCode::NotFound, "platform " + platform.str() + " is not registered");
  }
  out = it->second;
  return Status::success();
}

Status CapabilityRegistry::list_devices(std::vector<SubjectSnapshot>& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  out.clear();
  out.reserve(impl_->devices.size());
  for (const auto& entry : impl_->devices) {
    out.push_back(impl_->build_subject_locked(entry.second, EnvironmentContext{}));
  }
  return Status::success();
}

Status CapabilityRegistry::query_capability(const CapabilityQuery& query, ResolvedCapability& out) const {
  if (!query.device.valid() || !query.capability.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "capability query requires a device and a capability");
  }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const DeviceState* device = impl_->find_device_locked(query.device);
  if (device == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + query.device.str() + " is not registered");
  }
  out = impl_->resolve_locked(*device, query.capability, query.device_generation, query.environment);
  if (!query.include_evidence) out.evidence.clear();
  return Status::success();
}

Status CapabilityRegistry::query_capabilities(const DeviceCapabilityQuery& query,
                                              std::vector<ResolvedCapability>& out) const {
  out.clear();
  if (!query.device.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "device capability query requires a device");
  }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const DeviceState* device = impl_->find_device_locked(query.device);
  if (device == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + query.device.str() + " is not registered");
  }
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  // Device-scoped queries walk the per-device index, so their cost is
  // proportional to that device rather than to the whole registry.
  const auto owned = impl_->device_keys.find(query.device);
  if (owned == impl_->device_keys.end()) return Status::success();
  for (const CapabilityId& capability_id : owned->second) {
    if (!query.capability_prefix.empty()) {
      const std::string& token = capability_id.str();
      if (token.compare(0, query.capability_prefix.size(), query.capability_prefix) != 0) continue;
    }
    const auto key_state = impl_->keys.find(KeyId{query.device, capability_id});
    if (key_state == impl_->keys.end()) continue;
    const CapabilitySchema* schema = impl_->find_schema_locked(key_state->second.schema);
    if (query.domain.has_value()) {
      if (schema == nullptr || schema->domain != *query.domain) continue;
    }
    ResolvedCapability resolved =
        impl_->resolve_locked(*device, capability_id, query.device_generation, query.environment);
    if (query.only_current && !resolved.current) continue;
    if (query.only_stale && !is_stale(resolved.staleness)) continue;
    if (query.only_conditional && resolved.conditions == nullptr) continue;
    if (query.only_unknown && resolved.effective_support != EffectiveSupport::Unknown &&
        resolved.declared_support != SupportState::Unknown) {
      continue;
    }
    if (query.only_unsupported && resolved.effective_support != EffectiveSupport::Unsupported &&
        resolved.declared_support != SupportState::Unsupported) {
      continue;
    }
    if (query.declared_filter.has_value() && resolved.declared_support != *query.declared_filter) continue;
    if (query.effective_filter.has_value() && resolved.effective_support != *query.effective_filter) continue;
    if (query.truth_filter.has_value() && resolved.truth != *query.truth_filter) continue;
    out.push_back(std::move(resolved));
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::query_fleet(const FleetCapabilityQuery& query,
                                       std::vector<ResolvedCapability>& out) const {
  out.clear();
  if (!query.capability.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "fleet query requires a capability");
  }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  for (const auto& entry : impl_->devices) {
    if (query.hardware_class.has_value() && entry.second.identity.hardware.hardware_class != *query.hardware_class) {
      continue;
    }
    const auto key_it = impl_->keys.find(KeyId{entry.first, query.capability});
    if (key_it == impl_->keys.end()) continue;
    ResolvedCapability resolved =
        impl_->resolve_locked(entry.second, query.capability, DeviceGeneration{}, query.environment);
    if (query.only_current && !resolved.current) continue;
    if (query.effective_filter.has_value() && resolved.effective_support != *query.effective_filter) continue;
    if (query.truth_filter.has_value() && resolved.truth != *query.truth_filter) continue;
    out.push_back(std::move(resolved));
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::query_stale(const StaleCapabilityQuery& query,
                                       std::vector<ResolvedCapability>& out) const {
  out.clear();
  if (!query.device.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "stale query requires a device");
  }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const DeviceState* device = impl_->find_device_locked(query.device);
  if (device == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + query.device.str() + " is not registered");
  }
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  const auto owned = impl_->device_keys.find(query.device);
  if (owned == impl_->device_keys.end()) return Status::success();
  for (const CapabilityId& capability_id : owned->second) {
    ResolvedCapability resolved =
        impl_->resolve_locked(*device, capability_id, query.device_generation, query.environment);
    if (resolved.current && !is_stale(resolved.staleness)) continue;
    out.push_back(std::move(resolved));
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out) const {
  out.clear();
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  const DeviceState* device = nullptr;
  if (query.device.has_value()) {
    device = impl_->find_device_locked(*query.device);
    if (device == nullptr) {
      return Status::failure(ErrorCode::NotFound, "device " + query.device->str() + " is not registered");
    }
  }
  for (const auto& entry : impl_->quirks) {
    const QuirkRecord& quirk = entry.second;
    if (quirk.withdrawn) continue;
    if (query.capability.has_value() &&
        std::find(quirk.impacted_capabilities.begin(), quirk.impacted_capabilities.end(), *query.capability) ==
            quirk.impacted_capabilities.end()) {
      continue;
    }
    if (device == nullptr) {
      QuirkApplication application;
      application.id = quirk.id;
      application.quirk_generation = quirk.quirk_generation;
      application.severity = quirk.severity;
      application.mitigation = quirk.mitigation;
      application.title = quirk.title;
      application.result = QuirkApplicabilityResult::Unknown;
      application.reason = "no device supplied for applicability evaluation";
      if (query.only_applicable) continue;
      out.push_back(std::move(application));
      if (out.size() >= limit) break;
      continue;
    }
    QuirkApplication application = evaluate_quirk(quirk, device->identity, device->software, query.environment);
    if (query.only_applicable && application.result != QuirkApplicabilityResult::Applies) continue;
    out.push_back(std::move(application));
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::list_quirks(std::vector<QuirkRecord>& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  out.clear();
  for (const auto& entry : impl_->quirks) out.push_back(entry.second);
  return Status::success();
}

Status CapabilityRegistry::query_compatibility(const CompatibilityQuery& query,
                                               std::vector<CompatibilityFact>& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  out.clear();
  const std::size_t limit = query.max_results == 0 ? impl_->limits.max_query_results : query.max_results;
  for (const auto& entry : impl_->compatibility) {
    const CompatibilityFact& fact = entry.second;
    if (!query.include_withdrawn && fact.withdrawn) continue;
    if (query.kind.has_value() && fact.kind != *query.kind) continue;
    if (query.reference.has_value()) {
      const HardwareReference& reference = *query.reference;
      const bool matches = (fact.lhs.kind == reference.kind && fact.lhs.token == reference.token) ||
                           (fact.rhs.kind == reference.kind && fact.rhs.token == reference.token);
      if (!matches) continue;
    }
    out.push_back(fact);
    if (out.size() >= limit) break;
  }
  return Status::success();
}

Status CapabilityRegistry::compare_devices(const ComparisonRequest& request, DeviceComparison& out) const {
  if (!request.left.valid() || !request.right.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "comparison requires two device identities");
  }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const DeviceState* left = impl_->find_device_locked(request.left);
  const DeviceState* right = impl_->find_device_locked(request.right);
  if (left == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + request.left.str() + " is not registered");
  }
  if (right == nullptr) {
    return Status::failure(ErrorCode::NotFound, "device " + request.right.str() + " is not registered");
  }
  out = DeviceComparison{};
  out.left = request.left;
  out.right = request.right;
  out.left_generation = left->generation;
  out.right_generation = right->generation;
  out.left_software = left->software.driver.id.str() + "@" + left->software.driver.version;
  out.right_software = right->software.driver.id.str() + "@" + right->software.driver.version;

  std::set<CapabilityId> capabilities;
  for (const DeviceId& side : {request.left, request.right}) {
    const auto owned = impl_->device_keys.find(side);
    if (owned == impl_->device_keys.end()) continue;
    capabilities.insert(owned->second.begin(), owned->second.end());
  }
  for (const CapabilityId& capability : capabilities) {
    const bool in_left = impl_->keys.find(KeyId{request.left, capability}) != impl_->keys.end();
    const bool in_right = impl_->keys.find(KeyId{request.right, capability}) != impl_->keys.end();
    CapabilityDifference difference;
    difference.capability = capability;
    if (in_left && !in_right) {
      difference.kind = CapabilityDifferenceKind::OnlyInLeft;
      difference.detail = "capability knowledge recorded only for " + request.left.str();
    } else if (!in_left && in_right) {
      difference.kind = CapabilityDifferenceKind::OnlyInRight;
      difference.detail = "capability knowledge recorded only for " + request.right.str();
    } else {
      const ResolvedCapability left_view =
          impl_->resolve_locked(*left, capability, request.left_generation, request.environment);
      const ResolvedCapability right_view =
          impl_->resolve_locked(*right, capability, request.right_generation, request.environment);
      difference.left_effective = left_view.effective_support;
      difference.right_effective = right_view.effective_support;
      if (left_view.effective_support != right_view.effective_support) {
        difference.kind = CapabilityDifferenceKind::EffectiveSupportDiffers;
        difference.detail = std::string(to_string(left_view.effective_support)) + " vs " +
                            to_string(right_view.effective_support);
      } else if (left_view.declared_support != right_view.declared_support) {
        difference.kind = CapabilityDifferenceKind::SupportStateDiffers;
        difference.detail = std::string(to_string(left_view.declared_support)) + " vs " +
                            to_string(right_view.declared_support);
      } else if (!(left_view.value == right_view.value)) {
        difference.kind = CapabilityDifferenceKind::ValueDiffers;
        difference.detail = left_view.value.describe() + " vs " + right_view.value.describe();
      } else if (describe_condition(left_view.conditions.get()) !=
                 describe_condition(right_view.conditions.get())) {
        difference.kind = CapabilityDifferenceKind::ConditionsDiffer;
        difference.detail = describe_condition(left_view.conditions.get()) + " vs " +
                            describe_condition(right_view.conditions.get());
      } else if (left_view.truth != right_view.truth) {
        difference.kind = CapabilityDifferenceKind::TruthClassDiffers;
        difference.detail = std::string(to_string(left_view.truth)) + " vs " + to_string(right_view.truth);
      } else if (left_view.contradiction != right_view.contradiction) {
        difference.kind = CapabilityDifferenceKind::ContradictionDiffers;
        difference.detail = std::string(to_string(left_view.contradiction)) + " vs " +
                            to_string(right_view.contradiction);
      } else if (left_view.precision != right_view.precision) {
        difference.kind = CapabilityDifferenceKind::PrecisionDiffers;
        difference.detail = std::string(to_string(left_view.precision)) + " vs " +
                            to_string(right_view.precision);
      } else if (is_stale(left_view.staleness) || is_stale(right_view.staleness)) {
        difference.kind = CapabilityDifferenceKind::StalenessDiffers;
        difference.detail = describe_staleness(left_view.staleness) + " vs " +
                            describe_staleness(right_view.staleness);
      } else {
        difference.kind = CapabilityDifferenceKind::Equivalent;
        difference.detail = "equivalent under the queried environment";
        out.equivalent.push_back(capability);
      }
      if (difference.kind != CapabilityDifferenceKind::Equivalent &&
          left_view.declared_support != right_view.declared_support) {
        if (view_gate_axis(left_view, ConditionKind::MinDriverVersion) ||
            view_gate_axis(right_view, ConditionKind::MinDriverVersion)) {
          difference.kind = CapabilityDifferenceKind::DriverDependentDifference;
          difference.detail += " (driver-qualified)";
        } else if (view_gate_axis(left_view, ConditionKind::MinFirmwareVersion) ||
                   view_gate_axis(right_view, ConditionKind::MinFirmwareVersion)) {
          difference.kind = CapabilityDifferenceKind::FirmwareDependentDifference;
          difference.detail += " (firmware-qualified)";
        } else if (view_gate_axis(left_view, ConditionKind::MinRuntimeVersion) ||
                   view_gate_axis(right_view, ConditionKind::MinRuntimeVersion)) {
          difference.kind = CapabilityDifferenceKind::RuntimeDependentDifference;
          difference.detail += " (runtime-qualified)";
        }
      }
    }
    out.differences.push_back(std::move(difference));
    if (out.differences.size() >= impl_->limits.max_comparison_entries) break;
  }
  std::sort(out.differences.begin(), out.differences.end(),
            [](const CapabilityDifference& a, const CapabilityDifference& b) {
              if (a.capability != b.capability) return a.capability < b.capability;
              return static_cast<int>(a.kind) < static_cast<int>(b.kind);
            });
  std::sort(out.equivalent.begin(), out.equivalent.end());
  out.explanation = "comparison is factual: the registry reports differences, never an ordering";
  return Status::success();
}

Status CapabilityRegistry::snapshot(RegistrySnapshot& out) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->devices.size() > impl_->limits.max_snapshot_records) {
    return Status::failure(ErrorCode::LimitExceeded, "snapshot would exceed the configured record bound");
  }
  out = RegistrySnapshot{};
  impl_->snapshot_generation = impl_->snapshot_generation.next();
  out.generation = impl_->snapshot_generation;
  out.epoch = impl_->epoch;
  out.state_revision = impl_->state_revision;
  out.created_at_utc = utc_now_iso8601();
  for (const auto& entry : impl_->devices) {
    out.subjects.push_back(impl_->build_subject_locked(entry.second, EnvironmentContext{}));
  }
  for (const auto& entry : impl_->keys) {
    const DeviceState* device = impl_->find_device_locked(entry.first.first);
    if (device == nullptr) continue;
    ResolvedCapability resolved =
        impl_->resolve_locked(*device, entry.first.second, DeviceGeneration{}, EnvironmentContext{});
    for (const EvidenceView& view : resolved.evidence) {
      if (is_stale(view.staleness)) ++out.stale_evidence_records;
      if (!view.winner) continue;
      if (view.support == SupportState::Unsupported) {
        ++out.unsupported_statements;
        continue;
      }
      switch (view.source.provenance) {
        case ProvenanceClass::Synthetic: ++out.synthetic_statements; break;
        case ProvenanceClass::UnknownSource: ++out.unknown_statements; break;
        default: ++out.real_statements; break;
      }
    }
    out.capabilities.push_back(std::move(resolved));
    if (out.capabilities.size() >= impl_->limits.max_snapshot_records) break;
  }
  for (const auto& entry : impl_->quirks) out.quirks.push_back(entry.second);
  for (const auto& entry : impl_->compatibility) out.compatibility.push_back(entry.second);
  for (const auto& entry : impl_->publishers) out.publishers.push_back(entry.second.record);
  out.canonical_digest = canonical_snapshot_digest(out);
  ++impl_->usage.snapshots_taken;
  return Status::success();
}

Status CapabilityRegistry::explain_capability(const CapabilityQuery& query, std::string& out) const {
  ResolvedCapability resolved;
  const Status status = query_capability(query, resolved);
  if (!status.ok()) return status;
  out = resolved.explanation;
  return Status::success();
}

Status CapabilityRegistry::explain_device(const DeviceId& device, std::string& out) const {
  SubjectSnapshot subject;
  const Status status = find_device(device, subject);
  if (!status.ok()) return status;
  out = render_subject(subject);
  std::vector<ResolvedCapability> capabilities;
  DeviceCapabilityQuery query;
  query.device = device;
  query.only_current = false;
  const Status listed = query_capabilities(query, capabilities);
  if (!listed.ok()) return listed;
  for (const ResolvedCapability& capability : capabilities) {
    out += "\n";
    out += render_capability_brief(capability);
  }
  return Status::success();
}

CoordinatorEpoch CapabilityRegistry::coordinator_epoch() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->epoch;
}

Status CapabilityRegistry::advance_coordinator_epoch(CoordinatorEpoch& out) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->epoch = impl_->epoch.valid() ? impl_->epoch.next() : CoordinatorEpoch::first();
  out = impl_->epoch;
  impl_->touch_locked();
  return Status::success();
}

Status CapabilityRegistry::set_coordinator_epoch(CoordinatorEpoch epoch) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (!epoch.valid()) {
    return Status::failure(ErrorCode::InvalidArgument, "coordinator epoch must be positive");
  }
  if (impl_->epoch.valid() && epoch <= impl_->epoch) {
    return Status::failure(ErrorCode::GenerationRegression,
                           "coordinator epoch must strictly advance");
  }
  impl_->epoch = epoch;
  impl_->touch_locked();
  return Status::success();
}

std::uint64_t CapabilityRegistry::state_revision() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->state_revision;
}

Status CapabilityRegistry::begin_shutdown() {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->shutting_down = true;
  return Status::success();
}

bool CapabilityRegistry::shutting_down() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->shutting_down;
}

Status CapabilityRegistry::clear_shutdown() {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  impl_->shutting_down = false;
  return Status::success();
}

ResourceUsage CapabilityRegistry::resource_usage() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  ResourceUsage usage = impl_->usage;
  usage.devices = impl_->devices.size();
  usage.platforms = impl_->platforms.size();
  usage.capability_keys = impl_->keys.size();
  usage.evidence_records = impl_->evidence_count;
  usage.quirks = impl_->quirks.size();
  usage.compatibility_facts = impl_->compatibility.size();
  usage.publishers = impl_->publishers.size();
  usage.schemas = impl_->schemas.size();
  return usage;
}

DurableState CapabilityRegistry::export_durable_state() const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  DurableState state;
  state.header.format_version = state_format_version();
  state.header.epoch = impl_->epoch;
  state.header.snapshot_generation = impl_->snapshot_generation;
  state.header.state_revision = impl_->state_revision;
  state.header.created_at_utc = utc_now_iso8601();
  state.header.producer = std::string("HardwareCapabilityRegistry ") + version_string();
  for (const auto& entry : impl_->platforms) state.platforms.push_back(entry.second);
  for (const auto& entry : impl_->devices) {
    state.subjects.push_back(impl_->build_subject_locked(entry.second, EnvironmentContext{}));
  }
  for (const auto& entry : impl_->schemas) state.schemas.push_back(entry.second);
  for (const auto& entry : impl_->keys) {
    for (const StoredRecord& stored : entry.second.records) {
      // Superseded records carry a persistent flag. It is recomputed on import
      // from the retained ordering, so only the bare record is persisted.
      state.capability_history.push_back(stored.record);
    }
  }
  for (const auto& entry : impl_->quirks) state.quirks.push_back(entry.second);
  for (const auto& entry : impl_->compatibility) state.compatibility.push_back(entry.second);
  for (const auto& entry : impl_->publishers) {
    const PublisherStateInternal& publisher = entry.second;
    state.publisher_watermarks.push_back(Impl::watermark_for(entry.first, publisher));
    for (const PublisherBootId boot : publisher.fenced_boots) {
      if (boot == publisher.record.identity.boot) continue;
      PublisherWatermark fenced;
      fenced.publisher = entry.first;
      fenced.boot = boot;
      fenced.state = PublisherState::Fenced;
      fenced.reason = "boot superseded or fenced";
      state.fenced_boots.push_back(fenced);
    }
    if (publisher.publisher_fenced && publisher.record.identity.boot.valid()) {
      PublisherWatermark fenced;
      fenced.publisher = entry.first;
      fenced.boot = publisher.record.identity.boot;
      fenced.state = PublisherState::Fenced;
      fenced.reason = publisher.record.reason;
      state.fenced_boots.push_back(fenced);
    }
  }
  std::sort(state.platforms.begin(), state.platforms.end(),
            [](const PlatformIdentity& a, const PlatformIdentity& b) { return a.id < b.id; });
  std::sort(state.subjects.begin(), state.subjects.end(),
            [](const SubjectSnapshot& a, const SubjectSnapshot& b) { return a.identity.id < b.identity.id; });
  std::sort(state.capability_history.begin(), state.capability_history.end(),
            [](const CapabilityRecord& a, const CapabilityRecord& b) {
              if (a.device != b.device) return a.device < b.device;
              if (a.capability != b.capability) return a.capability < b.capability;
              if (a.capability_generation != b.capability_generation) {
                return a.capability_generation < b.capability_generation;
              }
              if (a.source.publisher.id != b.source.publisher.id) {
                return a.source.publisher.id < b.source.publisher.id;
              }
              return a.source.observation_sequence < b.source.observation_sequence;
            });
  std::sort(state.quirks.begin(), state.quirks.end(),
            [](const QuirkRecord& a, const QuirkRecord& b) { return a.id < b.id; });
  std::sort(state.compatibility.begin(), state.compatibility.end(),
            [](const CompatibilityFact& a, const CompatibilityFact& b) { return a.id < b.id; });
  std::sort(state.publisher_watermarks.begin(), state.publisher_watermarks.end(),
            [](const PublisherWatermark& a, const PublisherWatermark& b) {
              if (a.publisher != b.publisher) return a.publisher < b.publisher;
              return a.boot < b.boot;
            });
  std::sort(state.fenced_boots.begin(), state.fenced_boots.end(),
            [](const PublisherWatermark& a, const PublisherWatermark& b) {
              if (a.publisher != b.publisher) return a.publisher < b.publisher;
              return a.boot < b.boot;
            });
  state.canonical_digest = canonical_durable_digest(state);
  return state;
}

Status CapabilityRegistry::import_durable_state(const DurableState& state, ImportReport& report) {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  report = ImportReport{};
  if (state.header.format_version != state_format_version()) {
    return Status::failure(ErrorCode::UnsupportedVersion,
                           "state format version " + std::to_string(state.header.format_version) +
                               " is not supported by this build");
  }
  if (state.canonical_digest != 0) {
    const std::uint64_t digest = canonical_durable_digest(state);
    if (digest != state.canonical_digest) {
      return Status::failure(ErrorCode::IntegrityFailure, "durable state digest does not match its contents");
    }
  }
  if (state.subjects.size() > impl_->limits.max_devices) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the device bound");
  }
  if (state.capability_history.size() > impl_->limits.max_evidence_history) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the evidence bound");
  }
  if (state.quirks.size() > impl_->limits.max_quirks) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the quirk bound");
  }
  if (state.compatibility.size() > impl_->limits.max_compatibility_facts) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the compatibility bound");
  }
  if (state.publisher_watermarks.size() > impl_->limits.max_publishers * 64u) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the publisher bound");
  }

  // -- full validation before any mutation --------------------------------
  std::map<DeviceId, DeviceState> staged_devices;
  std::map<PlatformId, PlatformIdentity> staged_platforms;
  std::set<HardwareFamilyId> staged_families;
  std::set<HardwareModelId> staged_models;
  std::set<ArchitectureId> staged_architectures;
  for (const SubjectSnapshot& subject : state.subjects) {
    if (!subject.identity.id.valid() || !subject.identity.incarnation.valid()) {
      return Status::failure(ErrorCode::InvalidIdentity, "durable state contains an invalid device identity");
    }
    if (!subject.generation.valid() || !subject.boot.valid()) {
      return Status::failure(ErrorCode::InvalidIdentity,
                             "durable state contains an invalid device generation or boot");
    }
    const Status identity_status = validate_device_identity(subject.identity, impl_->limits);
    if (!identity_status.ok()) return identity_status;
    DeviceState device;
    device.identity = subject.identity;
    device.software = subject.software;
    device.generation = subject.generation;
    device.boot = subject.boot;
    device.platform_generation = subject.platform_generation.valid() ? subject.platform_generation
                                                                   : PlatformGeneration::first();
    if (staged_devices.find(subject.identity.id) != staged_devices.end()) {
      return Status::failure(ErrorCode::InvalidIdentity,
                             "durable state declares the same device identity twice");
    }
    staged_devices.emplace(subject.identity.id, device);
    // A family may legitimately contain many devices; the registry records the
    // set of known families so that family-level compatibility references can
    // be resolved without inventing a one-to-one mapping.
    if (subject.identity.hardware.family.valid()) {
      if (staged_families.size() >= impl_->limits.max_families &&
          staged_families.find(subject.identity.hardware.family) == staged_families.end()) {
        return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the hardware family bound");
      }
      staged_families.insert(subject.identity.hardware.family);
    }
    if (subject.identity.hardware.model.valid()) {
      staged_models.insert(subject.identity.hardware.model);
    }
    if (subject.identity.hardware.architecture.valid()) {
      staged_architectures.insert(subject.identity.hardware.architecture);
    }
  }
  for (const PlatformIdentity& platform : state.platforms) {
    if (!platform.id.valid()) {
      return Status::failure(ErrorCode::InvalidIdentity, "durable state contains an invalid platform identity");
    }
    staged_platforms.emplace(platform.id, platform);
  }

  std::map<CapabilitySchemaId, CapabilitySchema> staged_schemas = impl_->schemas;
  std::map<CapabilityId, CapabilitySchemaId> staged_capability_schemas = impl_->capability_schemas;
  for (const CapabilitySchema& schema : state.schemas) {
    const Status schema_status = validate_capability_schema(schema, impl_->limits);
    if (!schema_status.ok()) return schema_status;
    const auto existing = staged_schemas.find(schema.id);
    if (existing != staged_schemas.end()) {
      if (!(existing->second.capability == schema.capability &&
            existing->second.value_kind == schema.value_kind &&
            existing->second.domain == schema.domain &&
            existing->second.canonical_name == schema.canonical_name &&
            existing->second.unit == schema.unit)) {
        return Status::failure(ErrorCode::DuplicateConflict,
                               "durable state redefines schema " + schema.id.str() + " with different content");
      }
      continue;
    }
    const auto bound = staged_capability_schemas.find(schema.capability);
    if (bound != staged_capability_schemas.end()) {
      return Status::failure(ErrorCode::DuplicateConflict,
                             "durable state binds capability " + schema.capability.str() + " twice");
    }
    staged_capability_schemas.emplace(schema.capability, schema.id);
    staged_schemas.emplace(schema.id, schema);
  }

  std::map<KeyId, KeyState> staged_keys;
  std::size_t staged_records = 0;
  std::size_t revalidated = 0;
  for (const CapabilityRecord& record : state.capability_history) {
    const Status record_status = validate_capability_record(record, impl_->limits);
    if (!record_status.ok()) return record_status;
    const auto device_it = staged_devices.find(record.device);
    if (device_it == staged_devices.end()) {
      return Status::failure(ErrorCode::InvalidReference,
                             "durable state references unregistered device " + record.device.str());
    }
    if (staged_schemas.find(record.schema) == staged_schemas.end()) {
      return Status::failure(ErrorCode::InvalidCapability,
                             "durable state references unknown schema " + record.schema.str());
    }
    const auto schema_it = staged_schemas.find(record.schema);
    if (schema_it->second.capability != record.capability) {
      return Status::failure(ErrorCode::InvalidCapability,
                             "durable state binds capability " + record.capability.str() +
                                 " to a schema that describes another capability");
    }
    if (record.device_generation > device_it->second.generation) {
      return Status::failure(ErrorCode::StaleDeviceGeneration,
                             "durable state contains evidence for a future device generation");
    }
    if (!record.value.empty()) {
      const Status value_status = validate_value(record.value, impl_->limits);
      if (!value_status.ok()) return value_status;
    }
    KeyState& key = staged_keys[KeyId{record.device, record.capability}];
    if (!key.schema.valid()) key.schema = record.schema;
    if (key.schema != record.schema) {
      return Status::failure(ErrorCode::InvalidCapability,
                             "durable state binds one capability to several schemas");
    }
    if (record.capability_generation.valid()) {
      if (key.generation.valid() && record.capability_generation < key.generation) {
        return Status::failure(ErrorCode::GenerationRegression,
                               "durable state regresses a capability generation");
      }
      key.generation = record.capability_generation;
    }
    key.has_evidence = true;
    StoredRecord stored;
    stored.record = record;
    stored.slot = record.source.publisher.id.str() + "#" + record.source.publisher.boot.str() + "#" +
                  to_string(record.source.source_class) + "#" + record.source.native_reference + "#" +
                  to_string(record.precision);
    stored.serial = ++impl_->next_serial;
    bool superseded = false;
    for (const StoredRecord& other : key.records) {
      if (other.slot == stored.slot) superseded = true;
    }
    if (superseded) {
      for (StoredRecord& other : key.records) {
        if (other.slot == stored.slot) other.persistent |= StalenessFlag::SupersededByNewerEvidence;
      }
    }
    if (!provenance_is_durable(record.source.provenance)) ++revalidated;
    key.records.push_back(std::move(stored));
    ++staged_records;
  }

  std::map<QuirkId, QuirkRecord> staged_quirks;
  for (const QuirkRecord& quirk : state.quirks) {
    const Status quirk_status = validate_quirk_record(quirk, impl_->limits);
    if (!quirk_status.ok()) return quirk_status;
    for (const CapabilityId& capability : quirk.impacted_capabilities) {
      bool known = false;
      for (const auto& entry : staged_capability_schemas) {
        if (entry.first == capability) {
          known = true;
          break;
        }
      }
      if (!known) {
        return Status::failure(ErrorCode::InvalidReference,
                               "durable state quirk impacts unknown capability " + capability.str());
      }
    }
    if (staged_quirks.find(quirk.id) != staged_quirks.end()) {
      return Status::failure(ErrorCode::DuplicateConflict, "durable state declares a quirk twice");
    }
    staged_quirks.emplace(quirk.id, quirk);
  }

  std::map<CompatibilityFactId, CompatibilityFact> staged_compatibility;
  for (const CompatibilityFact& fact : state.compatibility) {
    const Status fact_status = validate_compatibility_fact(fact, impl_->limits);
    if (!fact_status.ok()) return fact_status;
    const auto resolves = [&](const HardwareReference& reference) {
      switch (reference.kind) {
        case HardwareReference::Kind::Device:
          return staged_devices.find(DeviceId::parse(reference.token)) != staged_devices.end();
        case HardwareReference::Kind::Platform:
          return staged_platforms.find(PlatformId::parse(reference.token)) != staged_platforms.end();
        case HardwareReference::Kind::HardwareFamily:
          return staged_families.find(HardwareFamilyId::parse(reference.token)) != staged_families.end();
        case HardwareReference::Kind::HardwareModel:
          return staged_models.find(HardwareModelId::parse(reference.token)) != staged_models.end();
        case HardwareReference::Kind::Architecture:
          return staged_architectures.find(ArchitectureId::parse(reference.token)) !=
                 staged_architectures.end();
        default:
          return true;
      }
    };
    if (!resolves(fact.lhs) || !resolves(fact.rhs)) {
      return Status::failure(ErrorCode::InvalidReference,
                             "durable state compatibility fact " + fact.id.str() + " has a dangling reference");
    }
    if (staged_compatibility.find(fact.id) != staged_compatibility.end()) {
      return Status::failure(ErrorCode::DuplicateConflict,
                             "durable state declares a compatibility fact twice");
    }
    staged_compatibility.emplace(fact.id, fact);
  }

  std::map<PublisherId, PublisherStateInternal> staged_publishers;
  for (const PublisherWatermark& watermark : state.publisher_watermarks) {
    if (!watermark.publisher.valid()) {
      return Status::failure(ErrorCode::InvalidIdentity, "durable state contains an invalid publisher id");
    }
    PublisherStateInternal& publisher = staged_publishers[watermark.publisher];
    if (!publisher.record.identity.id.valid()) {
      publisher.record.identity.id = watermark.publisher;
      publisher.record.adapter = watermark.adapter;
      publisher.record.registered_at_utc = watermark.registered_at_utc;
      publisher.record.accepted = watermark.accepted;
      publisher.record.rejected = watermark.rejected;
      publisher.record.identity.boot = watermark.boot;
      publisher.record.max_evidence_generation = watermark.max_evidence_generation;
    }
    if (publisher.watermarks.find(watermark.boot) != publisher.watermarks.end()) {
      return Status::failure(ErrorCode::DuplicateConflict,
                             "durable state declares a publisher boot watermark twice");
    }
    publisher.watermarks.emplace(watermark.boot, watermark);
    publisher.boot_history = publisher.boot_history + 1;
  }
  for (const PublisherWatermark& fenced : state.fenced_boots) {
    if (!fenced.publisher.valid() || !fenced.boot.valid()) {
      return Status::failure(ErrorCode::InvalidIdentity,
                             "durable state contains an invalid fenced boot identity");
    }
    PublisherStateInternal& publisher = staged_publishers[fenced.publisher];
    if (!publisher.record.identity.id.valid()) {
      publisher.record.identity.id = fenced.publisher;
    }
    publisher.fenced_boots.insert(fenced.boot);
  }
  if (staged_publishers.size() > impl_->limits.max_publishers) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the publisher bound");
  }

  // -- commit -------------------------------------------------------------
  impl_->devices = std::move(staged_devices);
  impl_->platforms = std::move(staged_platforms);
  impl_->families = std::move(staged_families);
  impl_->models = std::move(staged_models);
  impl_->architectures = std::move(staged_architectures);
  impl_->schemas = std::move(staged_schemas);
  impl_->capability_schemas = std::move(staged_capability_schemas);
  impl_->keys = std::move(staged_keys);
  // The per-device index is derived state and is rebuilt from the keys that
  // were just validated, so it can never disagree with them.
  impl_->device_keys.clear();
  for (const auto& entry : impl_->keys) {
    impl_->device_keys[entry.first.first].insert(entry.first.second);
  }
  impl_->quirks = std::move(staged_quirks);
  impl_->compatibility = std::move(staged_compatibility);
  impl_->publishers = std::move(staged_publishers);
  impl_->evidence_count = staged_records;
  impl_->global_order.clear();
  for (auto& entry : impl_->keys) {
    for (const StoredRecord& stored : entry.second.records) {
      impl_->global_order.push_back(GlobalEntry{entry.first, stored.serial});
    }
  }
  for (auto& entry : impl_->publishers) {
    // Process liveness is never restored: every publisher must re-register
    // with a fresh boot identity before its evidence can be authoritative.
    PublisherStateInternal& publisher = entry.second;
    if (publisher.record.identity.boot.valid()) {
      publisher.fenced_boots.insert(publisher.record.identity.boot);
    }
    publisher.record.state = PublisherState::Dead;
    if (publisher.record.reason.empty()) {
      publisher.record.reason = "coordinator restarted; publisher liveness is not restored";
    }
  }
  if (state.header.epoch.valid() && state.header.epoch > impl_->epoch) {
    impl_->epoch = state.header.epoch;
  }
  if (state.header.snapshot_generation.valid() &&
      state.header.snapshot_generation > impl_->snapshot_generation) {
    impl_->snapshot_generation = state.header.snapshot_generation;
  }
  impl_->state_revision = state.header.state_revision;
  impl_->touch_locked();

  report.applied = true;
  report.subjects = impl_->devices.size();
  report.schemas = state.schemas.size();
  report.capability_records = staged_records;
  report.revalidated_records = revalidated;
  report.quirks = impl_->quirks.size();
  report.compatibility_facts = impl_->compatibility.size();
  report.publishers = impl_->publishers.size();
  report.fenced_boots = state.fenced_boots.size();
  report.detail = "durable state applied; " + std::to_string(revalidated) +
                  " process-bound evidence records require revalidation";
  return Status::success();
}

}  // namespace hcr
