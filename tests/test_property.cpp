// Hardware Capability Registry - randomized property and deterministic race tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Every randomized test prints its seed on failure so a run can be reproduced
// exactly. Race tests force interleavings sequentially and assert that the
// final logical outcome does not depend on the order in which the operations
// were scheduled.
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/persistence.hpp"

using namespace hcr;
using namespace hcrtest;

namespace {

const char* const kCapabilities[] = {"cap.compute.fp32", "cap.compute.fp16", "cap.compute.int8",
                                     "cap.memory.capacity", "cap.memory.ecc", "cap.net.rdma"};

const SupportState kStates[] = {SupportState::SupportedNative, SupportState::SupportedConditional,
                                SupportState::SupportedEmulated, SupportState::Disabled,
                                SupportState::Unsupported, SupportState::Unknown};

CapabilityValue value_for(const char* capability, SupportState state, Random& random) {
  if (state == SupportState::Unsupported || state == SupportState::Unknown) return CapabilityValue{};
  const std::string name = capability;
  if (name == "cap.memory.capacity") {
    return CapabilityValue(BytesValue{1024ull * (1 + random.bounded(64))});
  }
  if (name == "cap.memory.ecc") {
    return CapabilityValue(EnumValue{"ecc", random.chance(1, 2) ? "enabled" : "disabled"});
  }
  return CapabilityValue(true);
}

SourceClass source_for(Random& random) {
  switch (random.bounded(5)) {
    case 0: return SourceClass::HardwareProbe;
    case 1: return SourceClass::RuntimeProbe;
    case 2: return SourceClass::StaticCuratedDatabase;
    case 3: return SourceClass::Nvml;
    default: return SourceClass::SyntheticBackend;
  }
}

ProvenanceClass provenance_for(SourceClass source) { return default_provenance_for(source); }

}  // namespace

HCR_TEST(randomized, invariants_hold_across_generated_operation_sequences) {
  const std::vector<std::uint64_t> seeds = {1u, 2u, 3u, 17u, 4099u, 65537u, 999983u};
  for (const std::uint64_t seed : seeds) {
    Random random(seed);
    Fixture fixture;
    const std::vector<std::string> devices = {"alpha", "beta", "gamma"};
    for (const std::string& device : devices) {
      const ApplyResult registered =
          fixture.add_device(device, random.chance(1, 2) ? "1.0" : "2.0", "1.0");
      HCR_CHECK_MSG(registered.accepted(), "seed " + std::to_string(seed));
    }

    std::map<std::string, CapabilityGeneration> last_generation;
    std::map<std::string, std::map<std::uint64_t, SupportState>> historical;

    for (int step = 0; step < 60; ++step) {
      const std::string& device = devices[random.bounded(devices.size())];
      const char* capability = kCapabilities[random.bounded(sizeof(kCapabilities) / sizeof(kCapabilities[0]))];
      const int operation = random.between(0, 7);
      switch (operation) {
        case 0:
        case 1:
        case 2:
        case 3: {
          const SupportState state = kStates[random.bounded(sizeof(kStates) / sizeof(kStates[0]))];
          const SourceClass source = source_for(random);
          fixture.publish(device, capability, state, value_for(capability, state, random),
                          PrecisionClass::DirectReported, "generated", source, provenance_for(source));
          break;
        }
        case 4:
          fixture.add_device(device, random.chance(1, 2) ? "1.0" : "3.0", "1.0");
          break;
        case 5:
          fixture.reset_device(device);
          break;
        case 6: {
          const SupportState state = kStates[random.bounded(sizeof(kStates) / sizeof(kStates[0]))];
          const SourceClass source = source_for(random);
          fixture.publish(device, capability, state, value_for(capability, state, random),
                          PrecisionClass::DirectReported, "generated", source, provenance_for(source),
                          random.chance(1, 2) ? min_driver("500.0") : nullptr);
          break;
        }
        default:
          fixture.publish_quirk(("quirk.gen." + std::to_string(step)).c_str(), capability,
                                random.chance(1, 3) ? QuirkSeverity::Unusable : QuirkSeverity::Caveat,
                                QuirkGeneration::first(), "family.test");
          break;
      }

      // Invariant 1: one device identity, one current generation, unique ids.
      std::vector<SubjectSnapshot> subjects;
      HCR_CHECK_STATUS(fixture.registry.list_devices(subjects));
      std::map<DeviceId, std::size_t> occurrences;
      for (const SubjectSnapshot& subject : subjects) {
        ++occurrences[subject.identity.id];
        HCR_CHECK(subject.generation.valid());
        HCR_CHECK(subject.boot.valid());
      }
      for (const auto& entry : occurrences) HCR_CHECK_EQ(entry.second, std::size_t{1});
      HCR_CHECK_EQ(subjects.size(), devices.size());

      // Invariant 2: capability generations never regress, historical evidence
      // is immutable, and states stay coherent.
      for (const std::string& device_name : devices) {
        DeviceCapabilityQuery query;
        query.device = fixture.device_of(device_name);
        query.only_current = false;
        std::vector<ResolvedCapability> capabilities;
        HCR_CHECK_STATUS(fixture.registry.query_capabilities(query, capabilities));
        for (const ResolvedCapability& resolved : capabilities) {
          const std::string key = device_name + "/" + resolved.capability.str();
          if (resolved.capability_generation.valid()) {
            const auto previous = last_generation.find(key);
            if (previous != last_generation.end()) {
              HCR_CHECK_MSG(resolved.capability_generation >= previous->second,
                            "seed " + std::to_string(seed) + " key " + key);
            }
            last_generation[key] = resolved.capability_generation;
          }
          // UNKNOWN must never become SUPPORTED.
          if (resolved.declared_support == SupportState::Unknown) {
            HCR_CHECK(resolved.effective_support != EffectiveSupport::Supported);
          }
          // UNSUPPORTED is positive evidence and stays distinct.
          if (resolved.declared_support == SupportState::Unsupported) {
            HCR_CHECK(resolved.effective_support == EffectiveSupport::Unsupported ||
                      resolved.effective_support == EffectiveSupport::RevalidationRequired);
          }
          if (!resolved.current) {
            HCR_CHECK(resolved.effective_support == EffectiveSupport::RevalidationRequired);
          }
          for (const EvidenceView& view : resolved.evidence) {
            const std::uint64_t generation = view.capability_generation.value();
            auto& observed = historical[key];
            const auto existing = observed.find(generation);
            if (existing == observed.end()) {
              observed[generation] = view.support;
            } else {
              HCR_CHECK_MSG(existing->second == view.support,
                            "historical evidence mutated: seed " + std::to_string(seed) + " key " + key +
                                " generation " + std::to_string(generation) + " was " +
                                ::hcr::to_string(existing->second) + " now " + ::hcr::to_string(view.support) +
                                " adapter " + view.source.adapter + " reference " +
                                view.source.native_reference + " answer-generation " +
                                resolved.capability_generation.str() + " brief " +
                                render_capability_brief(resolved));
            }
          }
        }
      }
    }

    // Invariant 3: identical state yields identical answers.
    const auto render_all = [&fixture, &devices]() {
      std::string rendered;
      for (const std::string& device_name : devices) {
        DeviceCapabilityQuery query;
        query.device = fixture.device_of(device_name);
        query.only_current = false;
        std::vector<ResolvedCapability> capabilities;
        fixture.registry.query_capabilities(query, capabilities);
        for (const ResolvedCapability& resolved : capabilities) {
          rendered += render_capability_brief(resolved);
          rendered += "|";
          rendered += resolved.explanation;
          rendered += "\n";
        }
      }
      RegistrySnapshot snapshot;
      fixture.registry.snapshot(snapshot);
      rendered += hex64(canonical_snapshot_digest(snapshot));
      return rendered;
    };
    HCR_CHECK_EQ(render_all(), render_all());
  }
}

HCR_TEST(randomized, persistence_round_trip_is_deterministic) {
  const std::uint64_t seed = 20260131u;
  Random random(seed);
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("persist-a").accepted());
  HCR_REQUIRE(fixture.add_device("persist-b").accepted());
  for (int step = 0; step < 40; ++step) {
    const std::string device = random.chance(1, 2) ? "persist-a" : "persist-b";
    const char* capability = kCapabilities[random.bounded(sizeof(kCapabilities) / sizeof(kCapabilities[0]))];
    const SupportState state = kStates[random.bounded(sizeof(kStates) / sizeof(kStates[0]))];
    const SourceClass source = source_for(random);
    fixture.publish(device, capability, state, value_for(capability, state, random),
                    PrecisionClass::DirectReported, "generated", source, provenance_for(source));
  }

  const DurableState state = fixture.registry.export_durable_state();
  PersistOptions options;
  std::vector<std::uint8_t> first_bytes;
  std::vector<std::uint8_t> second_bytes;
  HCR_CHECK_STATUS(encode_durable_state(state, options, first_bytes));
  HCR_CHECK_STATUS(encode_durable_state(state, options, second_bytes));
  HCR_CHECK(first_bytes == second_bytes);

  DurableState decoded;
  HCR_CHECK_STATUS(decode_durable_state(first_bytes, options, decoded));
  HCR_CHECK_EQ(decoded.canonical_digest, state.canonical_digest);

  const auto render_registry = [](CapabilityRegistry& registry, const char* device_token) {
    DeviceCapabilityQuery query;
    query.device = canonical_device_id("dev.test", device_token);
    query.only_current = false;
    std::vector<ResolvedCapability> capabilities;
    registry.query_capabilities(query, capabilities);
    std::string rendered;
    for (const ResolvedCapability& resolved : capabilities) {
      rendered += render_capability_brief(resolved);
      rendered += "\n";
    }
    return rendered;
  };

  CapabilityRegistry restored_one;
  CapabilityRegistry restored_two;
  ImportReport report_one;
  ImportReport report_two;
  HCR_CHECK_STATUS(restored_one.import_durable_state(decoded, report_one));
  HCR_CHECK_STATUS(restored_two.import_durable_state(decoded, report_two));
  HCR_CHECK_EQ(render_registry(restored_one, "persist-a"), render_registry(restored_two, "persist-a"));
  HCR_CHECK_EQ(render_registry(restored_one, "persist-b"), render_registry(restored_two, "persist-b"));
  HCR_CHECK_EQ(report_one.revalidated_records, report_two.revalidated_records);
}

HCR_TEST(race, device_reset_versus_publication_has_one_logical_outcome) {
  const auto run = [](bool publish_first) {
    Fixture fixture;
    HCR_REQUIRE(fixture.add_device("race-reset").accepted());
    const DeviceGeneration generation = fixture.generation_of("race-reset");
    if (publish_first) {
      fixture.publish("race-reset", "cap.compute.fp32", SupportState::SupportedNative, CapabilityValue(true),
                      PrecisionClass::DirectReported, "race", SourceClass::RuntimeProbe,
                      ProvenanceClass::RealRuntimeProbe, nullptr, generation);
      fixture.reset_device("race-reset");
    } else {
      // Capture the identity of the generation the publication will carry,
      // then reset before delivering it.
      fixture.reset_device("race-reset");
      const ApplyResult stale = fixture.publish(
          "race-reset", "cap.compute.fp32", SupportState::SupportedNative, CapabilityValue(true),
          PrecisionClass::DirectReported, "race", SourceClass::RuntimeProbe,
          ProvenanceClass::RealRuntimeProbe, nullptr, generation);
      HCR_CHECK_EQ(stale.code, ApplyCode::RejectedStaleDeviceGeneration);
    }
    fixture.publish("race-reset", "cap.compute.fp32", SupportState::SupportedNative, CapabilityValue(true),
                    PrecisionClass::DirectReported, "race", SourceClass::RuntimeProbe,
                    ProvenanceClass::RealRuntimeProbe);
    return fixture.query("race-reset", "cap.compute.fp32");
  };

  const ResolvedCapability published_first = run(true);
  const ResolvedCapability reset_first = run(false);
  HCR_CHECK_EQ(published_first.declared_support, reset_first.declared_support);
  HCR_CHECK_EQ(published_first.effective_support, reset_first.effective_support);
  HCR_CHECK_EQ(published_first.truth, reset_first.truth);
  HCR_CHECK_EQ(published_first.current, reset_first.current);
  HCR_CHECK_EQ(published_first.contradiction, reset_first.contradiction);
}

HCR_TEST(race, publisher_fence_versus_publication_has_one_logical_outcome) {
  const auto run = [](bool publish_first) {
    Fixture fixture;
    HCR_REQUIRE(fixture.add_device("race-fence").accepted());
    if (publish_first) {
      fixture.publish("race-fence", "cap.compute.fp16", SupportState::SupportedNative,
                      CapabilityValue(true));
      fixture.registry.retire_publisher_boot(fixture.publisher.id, fixture.publisher.boot, "died");
    } else {
      fixture.registry.retire_publisher_boot(fixture.publisher.id, fixture.publisher.boot, "died");
      const ApplyResult rejected = fixture.publish("race-fence", "cap.compute.fp16",
                                                   SupportState::SupportedNative, CapabilityValue(true));
      HCR_CHECK_EQ(rejected.code, ApplyCode::RejectedFencedPublisher);
    }
    return fixture.query("race-fence", "cap.compute.fp16");
  };
  const ResolvedCapability published_first = run(true);
  const ResolvedCapability fenced_first = run(false);
  // The two orders differ in what was retained (evidence that arrived before
  // the fence is kept for audit; a publication that arrived after it was never
  // recorded), but in neither order is the capability current or usable.
  HCR_CHECK(!published_first.current);
  HCR_CHECK(!fenced_first.current);
  HCR_CHECK(!published_first.usable());
  HCR_CHECK(!fenced_first.usable());
  HCR_CHECK(published_first.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK(fenced_first.effective_support == EffectiveSupport::Unknown);
}

HCR_TEST(race, quirk_publication_versus_capability_publication_is_order_independent) {
  const auto run = [](bool quirk_first) {
    Fixture fixture;
    HCR_REQUIRE(fixture.add_device("race-quirk", "535.0").accepted());
    if (quirk_first) {
      fixture.publish_quirk("quirk.race", "cap.interconnect.peer_to_peer", QuirkSeverity::Unusable,
                            QuirkGeneration::first(), "family.test", "530.0", "540.0");
      fixture.publish("race-quirk", "cap.interconnect.peer_to_peer", SupportState::SupportedNative,
                      CapabilityValue(true));
    } else {
      fixture.publish("race-quirk", "cap.interconnect.peer_to_peer", SupportState::SupportedNative,
                      CapabilityValue(true));
      fixture.publish_quirk("quirk.race", "cap.interconnect.peer_to_peer", QuirkSeverity::Unusable,
                            QuirkGeneration::first(), "family.test", "530.0", "540.0");
    }
    return fixture.query("race-quirk", "cap.interconnect.peer_to_peer");
  };
  const ResolvedCapability quirk_first = run(true);
  const ResolvedCapability capability_first = run(false);
  HCR_CHECK_EQ(quirk_first.effective_support, capability_first.effective_support);
  HCR_CHECK(quirk_first.effective_support == EffectiveSupport::BlockedByQuirk);
  HCR_CHECK_EQ(render_capability_brief(quirk_first), render_capability_brief(capability_first));
}

HCR_TEST(race, contradictory_evidence_arrival_order_is_order_independent) {
  const auto run = [](bool live_first) {
    Fixture fixture;
    HCR_REQUIRE(fixture.add_device("race-conflict").accepted());
    const auto curated = [&fixture]() {
      fixture.publish("race-conflict", "cap.compute.fp8", SupportState::SupportedNative,
                      CapabilityValue(true), PrecisionClass::Curated, "curated.table",
                      SourceClass::StaticCuratedDatabase, ProvenanceClass::CuratedStatic);
    };
    const auto live = [&fixture]() {
      fixture.publish("race-conflict", "cap.compute.fp8", SupportState::Unsupported, CapabilityValue{},
                      PrecisionClass::Exact, "runtime.probe", SourceClass::RuntimeProbe,
                      ProvenanceClass::RealRuntimeProbe);
    };
    if (live_first) {
      live();
      curated();
    } else {
      curated();
      live();
    }
    return fixture.query("race-conflict", "cap.compute.fp8");
  };
  const ResolvedCapability live_first = run(true);
  const ResolvedCapability curated_first = run(false);
  HCR_CHECK_EQ(live_first.contradiction, curated_first.contradiction);
  HCR_CHECK(live_first.contradiction == ContradictionState::PreferredLiveEvidence);
  HCR_CHECK_EQ(live_first.declared_support, curated_first.declared_support);
  HCR_CHECK_EQ(live_first.truth, curated_first.truth);
  HCR_CHECK_EQ(live_first.effective_support, curated_first.effective_support);
  HCR_CHECK_EQ(live_first.precision, curated_first.precision);
  HCR_CHECK_EQ(live_first.capability_generation.value(), curated_first.capability_generation.value());
  HCR_CHECK_EQ(live_first.value.describe(), curated_first.value.describe());
  HCR_CHECK_EQ(render_capability_brief(live_first), render_capability_brief(curated_first));
}

HCR_TEST(race, snapshot_taken_during_mutation_never_observes_later_evidence) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("race-snapshot").accepted());
  HCR_REQUIRE(fixture.publish("race-snapshot", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  RegistrySnapshot before;
  HCR_CHECK_STATUS(fixture.registry.snapshot(before));
  const std::size_t count_before = before.capabilities.size();

  HCR_REQUIRE(fixture.publish("race-snapshot", "cap.compute.fp16", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  RegistrySnapshot after;
  HCR_CHECK_STATUS(fixture.registry.snapshot(after));
  HCR_CHECK_EQ(before.capabilities.size(), count_before);
  HCR_CHECK(after.capabilities.size() > before.capabilities.size());
  HCR_CHECK(!snapshot_is_current(before, after));
}

HCR_TEST(race, shutdown_versus_publication_leaves_consistent_state) {
  Fixture fixture;
  HCR_REQUIRE(fixture.add_device("race-shutdown").accepted());
  HCR_REQUIRE(fixture.publish("race-shutdown", "cap.compute.fp32", SupportState::SupportedNative,
                              CapabilityValue(true))
                  .accepted());
  HCR_CHECK_STATUS(fixture.registry.begin_shutdown());
  const ResolvedCapability during = fixture.query("race-shutdown", "cap.compute.fp32");
  HCR_CHECK(during.effective_support == EffectiveSupport::Supported);
  const ApplyResult rejected = fixture.publish("race-shutdown", "cap.compute.fp16",
                                               SupportState::SupportedNative, CapabilityValue(true));
  HCR_CHECK_EQ(rejected.code, ApplyCode::RejectedShuttingDown);
  const ResolvedCapability after = fixture.query("race-shutdown", "cap.compute.fp32");
  HCR_CHECK_EQ(render_capability_brief(during), render_capability_brief(after));
}
