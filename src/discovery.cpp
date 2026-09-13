// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/discovery.hpp"

#include <string>
#include <utility>

#include "hcr/canonical.hpp"
#include "hcr/registry.hpp"
#include "hcr/time.hpp"

namespace hcr {

EvidenceSink::~EvidenceSink() = default;
IDiscoveryBackend::~IDiscoveryBackend() = default;

ApplyResult LocalEvidenceSink::ensure_publisher(const PublisherIdentity& identity, std::string adapter) {
  std::lock_guard<std::mutex> guard(mutex_);
  const auto existing = assigned_.find(identity.id);
  if (existing != assigned_.end()) {
    // Re-affirm the existing identity rather than requesting a new boot.
    const ApplyResult reaffirmed = registry_.register_publisher(existing->second, adapter);
    if (reaffirmed.accepted() || reaffirmed.code == ApplyCode::AcceptedIdempotent) {
      ApplyResult result;
      result.code = ApplyCode::Accepted;
      result.assigned_publisher = existing->second;
      result.detail = "publisher identity reused by this process";
      return result;
    }
    return reaffirmed;
  }
  const ApplyResult registered = registry_.register_publisher(identity, std::move(adapter));
  if (registered.accepted()) assigned_.emplace(identity.id, registered.assigned_publisher);
  return registered;
}

std::uint64_t LocalEvidenceSink::next_sequence(const PublisherId& publisher) {
  std::lock_guard<std::mutex> guard(mutex_);
  return ++sequences_[publisher];
}

std::size_t LocalEvidenceSink::publisher_count() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return assigned_.size();
}

ApplyResult LocalEvidenceSink::publish(const Publication& publication) { return registry_.apply(publication); }

DeviceId canonical_device_id(std::string_view kind, std::string_view token) {
  std::uint64_t hash = kFnvOffsetBasis;
  fnv1a64_mix(hash, kind);
  fnv1a64_mix(hash, token);
  return DeviceId::parse(std::string(kind) + "." + hex64(hash));
}

DeviceId host_device_id(const PlatformIdentity& platform) {
  std::string token = platform.id.str();
  token += "|";
  token += platform.vendor;
  token += "|";
  token += platform.model;
  return canonical_device_id("dev.host", token);
}

PublicationBuilder::PublicationBuilder(EvidenceSink& sink, DiscoveryContext context)
    : sink_(sink), context_(std::move(context)) {
  if (!context_.evidence_generation.valid()) context_.evidence_generation = EvidenceGeneration::first();
  if (context_.observed_at_utc.empty()) context_.observed_at_utc = utc_now_iso8601();
  report_.adapter = context_.adapter;
}

ApplyResult PublicationBuilder::note(ApplyResult result, const char* what) {
  if (result.code == ApplyCode::Accepted) {
    ++report_.accepted;
  } else if (result.code == ApplyCode::AcceptedIdempotent) {
    ++report_.idempotent;
  } else {
    ++report_.rejected;
    report_.notes.push_back(std::string(what) + ": " + to_string(result.code) + ": " + result.detail);
  }
  return result;
}

std::uint64_t PublicationBuilder::take_sequence() { return sink_.next_sequence(context_.publisher.id); }

Status PublicationBuilder::submit(Publication publication) {
  if (publication.capability.source.source_class == SourceClass::Unspecified) {
    publication.capability.source.source_class = context_.source_class;
    publication.capability.source.provenance = context_.provenance;
  }
  if (publication.quirk.source.source_class == SourceClass::Unspecified) {
    publication.quirk.source.source_class = context_.source_class;
    publication.quirk.source.provenance = context_.provenance;
  }
  if (publication.compatibility.source.source_class == SourceClass::Unspecified) {
    publication.compatibility.source.source_class = context_.source_class;
    publication.compatibility.source.provenance = context_.provenance;
  }
  publication.publisher = context_.publisher;
  publication.sequence = take_sequence();
  publication.evidence_generation = context_.evidence_generation;
  publication.issued_at_utc = context_.observed_at_utc;
  std::string what = to_string(publication.kind);
  if (publication.capability.capability.valid()) {
    what += " ";
    what += publication.capability.capability.str();
  } else if (publication.quirk.id.valid()) {
    what += " ";
    what += publication.quirk.id.str();
  } else if (publication.compatibility.id.valid()) {
    what += " ";
    what += publication.compatibility.id.str();
  }
  const ApplyResult result = note(sink_.publish(publication), what.c_str());
  if (result.accepted()) return Status::success();
  return Status::failure(ErrorCode::InvalidArgument,
                         std::string(to_string(result.code)) + ": " + result.detail);
}

Status PublicationBuilder::register_subject(const DeviceRegistration& registration, SubjectContext& out) {
  Publication publication;
  publication.kind = PublicationKind::RegisterDevice;
  publication.device = registration;
  publication.publisher = context_.publisher;
  publication.sequence = take_sequence();
  publication.evidence_generation = context_.evidence_generation;
  publication.issued_at_utc = context_.observed_at_utc;
  const ApplyResult result = note(sink_.publish(publication), "register-device");
  if (!result.accepted()) {
    return Status::failure(ErrorCode::InvalidArgument,
                           std::string(to_string(result.code)) + ": " + result.detail);
  }
  out.device = registration.identity.id;
  out.generation = result.device_generation;
  out.boot = result.device_boot;
  return Status::success();
}

Status PublicationBuilder::announce_device_generation(const DeviceRegistration& registration,
                                                      SubjectContext& out) {
  Publication publication;
  publication.kind = PublicationKind::PublishDeviceGeneration;
  publication.device = registration;
  publication.device.device_boot_observed = true;
  publication.publisher = context_.publisher;
  publication.sequence = take_sequence();
  publication.evidence_generation = context_.evidence_generation;
  publication.issued_at_utc = context_.observed_at_utc;
  const ApplyResult result = note(sink_.publish(publication), "publish-device-generation");
  if (!result.accepted()) {
    return Status::failure(ErrorCode::InvalidArgument,
                           std::string(to_string(result.code)) + ": " + result.detail);
  }
  out.device = registration.identity.id;
  out.generation = result.device_generation;
  out.boot = result.device_boot;
  return Status::success();
}

Status PublicationBuilder::publish_capability(const SubjectContext& subject,
                                              const CapabilityId& capability,
                                              SupportState support,
                                              CapabilityValue value,
                                              PrecisionClass precision,
                                              std::string native_reference,
                                              ConditionPtr conditions,
                                              SourceClass source_class,
                                              ProvenanceClass provenance) {
  Publication publication;
  publication.kind = PublicationKind::PublishCapability;
  publication.capability.device = subject.device;
  publication.capability.device_generation = subject.generation;
  publication.capability.device_boot = subject.boot;
  publication.capability.capability = capability;
  publication.capability.support = support;
  publication.capability.value = std::move(value);
  publication.capability.precision = precision;
  publication.capability.conditions = std::move(conditions);
  publication.capability.source.source_class = source_class;
  publication.capability.source.provenance = provenance;
  publication.capability.source.adapter =
      context_.adapter.empty() ? report_.adapter : context_.adapter;
  publication.capability.source.native_reference = std::move(native_reference);
  publication.capability.source.observed_at_utc = context_.observed_at_utc;
  publication.capability.source.evidence_generation = context_.evidence_generation;
  return submit(std::move(publication));
}

Status PublicationBuilder::publish_quirk(const QuirkRecord& quirk) {
  Publication publication;
  publication.kind = quirk.withdrawn ? PublicationKind::WithdrawQuirk : PublicationKind::PublishQuirk;
  publication.quirk = quirk;
  publication.quirk.source.adapter = context_.adapter.empty() ? report_.adapter : context_.adapter;
  publication.quirk.source.evidence_generation = context_.evidence_generation;
  if (publication.quirk.source.observed_at_utc.empty()) {
    publication.quirk.source.observed_at_utc = context_.observed_at_utc;
  }
  return submit(std::move(publication));
}

Status PublicationBuilder::publish_compatibility(const CompatibilityFact& compatibility) {
  Publication publication;
  publication.kind = compatibility.withdrawn ? PublicationKind::WithdrawCompatibility
                                             : PublicationKind::PublishCompatibility;
  publication.compatibility = compatibility;
  publication.compatibility.source.adapter =
      context_.adapter.empty() ? report_.adapter : context_.adapter;
  publication.compatibility.source.evidence_generation = context_.evidence_generation;
  if (publication.compatibility.source.observed_at_utc.empty()) {
    publication.compatibility.source.observed_at_utc = context_.observed_at_utc;
  }
  return submit(std::move(publication));
}

Status PublicationBuilder::register_schema(const CapabilitySchema& schema) {
  Publication publication;
  publication.kind = PublicationKind::RegisterCapabilitySchema;
  publication.schema = schema;
  return submit(std::move(publication));
}

Status PublicationBuilder::heartbeat() {
  Publication publication;
  publication.kind = PublicationKind::Heartbeat;
  return submit(std::move(publication));
}

void PublicationBuilder::bump_evidence_generation() {
  context_.evidence_generation = context_.evidence_generation.valid() ? context_.evidence_generation.next()
                                                                     : EvidenceGeneration::first();
}

Status run_discovery(IDiscoveryBackend& backend, const DiscoveryContext& context, EvidenceSink& sink,
                     DiscoveryReport& report) {
  report = DiscoveryReport{};
  report.adapter = backend.adapter_name();
  std::string reason;
  if (!backend.available(reason)) {
    report.available = false;
    report.notes.push_back(reason.empty() ? "adapter reports itself unavailable" : reason);
    return Status::success();
  }
  report.available = true;
  const ApplyResult registered = sink.ensure_publisher(context.publisher, backend.adapter_name());
  if (!registered.accepted()) {
    report.notes.push_back(std::string("publisher registration failed: ") + to_string(registered.code) +
                           ": " + registered.detail);
    return Status::failure(ErrorCode::InvalidProvenance,
                           "discovery adapter " + backend.adapter_name() + " could not register: " +
                               registered.detail);
  }
  DiscoveryContext effective = context;
  effective.publisher = registered.assigned_publisher;
  effective.source_class = backend.source_class();
  effective.provenance = backend.provenance();
  effective.adapter = backend.adapter_name();
  // An empty environment is passed through untouched: conditions evaluated
  // against it resolve to Unknown rather than to Satisfied.
  const Status status = backend.discover(effective, sink, report);
  // The adapter replaces the report with its own outcome; availability and the
  // adapter name are the harness's own conclusions and are restored here.
  report.adapter = backend.adapter_name();
  report.available = true;
  return status;
}

}  // namespace hcr
