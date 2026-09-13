// Hardware Capability Registry - completed-operation benchmarks.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Every number printed here was measured on the machine that ran it. The
// benchmark exercises the production ingestion and query paths, including the
// published API a downstream consumer uses, and reports median and best-of
// timings so that a single scheduling hiccup cannot masquerade as a result.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "hcr/adapters.hpp"
#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"
#include "hcr/discovery.hpp"
#include "hcr/persistence.hpp"
#include "hcr/registry.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"
#include "hcr/version.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double millis_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Measurement {
  double best_ms = 0.0;
  double median_ms = 0.0;
  double per_operation_us = 0.0;
};

/// Runs the body repeatedly and keeps the best and median wall times.
template <typename Body>
Measurement measure(int repeats, std::size_t operations, Body&& body) {
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(repeats));
  for (int i = 0; i < repeats; ++i) {
    const Clock::time_point start = Clock::now();
    body();
    samples.push_back(millis_since(start));
  }
  std::sort(samples.begin(), samples.end());
  Measurement measurement;
  measurement.best_ms = samples.front();
  measurement.median_ms = samples[samples.size() / 2];
  measurement.per_operation_us =
      operations == 0 ? 0.0 : (measurement.median_ms * 1000.0) / static_cast<double>(operations);
  return measurement;
}

void report(const char* label, std::size_t scale, const Measurement& measurement, std::size_t operations) {
  std::printf("%-38s devices=%-6zu ops=%-8zu best=%9.3f ms median=%9.3f ms per-op=%8.3f us\n", label,
              scale, operations, measurement.best_ms, measurement.median_ms, measurement.per_operation_us);
}

struct Fixture {
  hcr::CapabilityRegistry registry;
  hcr::PublisherIdentity publisher;
  std::vector<hcr::DeviceId> devices;
  std::vector<hcr::DeviceRegistration> registrations;
};

const char* const kCapabilities[] = {"cap.compute.fp32",  "cap.compute.fp16",  "cap.compute.int8",
                                     "cap.memory.capacity", "cap.memory.bandwidth", "cap.memory.ecc",
                                     "cap.net.rdma",       "cap.runtime.ipc"};

void build_fleet(Fixture& fixture, std::size_t device_count, bool with_capabilities) {
  hcr::PublisherIdentity identity;
  identity.id = hcr::PublisherId::parse("publisher.benchmark");
  const hcr::ApplyResult registered = fixture.registry.register_publisher(identity, "benchmark");
  fixture.publisher = registered.assigned_publisher;
  fixture.devices.reserve(device_count);
  fixture.registrations.reserve(device_count);
  std::uint64_t sequence = 0;
  for (std::size_t index = 0; index < device_count; ++index) {
    hcr::DeviceRegistration registration;
    const std::string token = "device-" + std::to_string(index);
    registration.identity.id = hcr::canonical_device_id("dev.benchmark", token);
    registration.identity.incarnation = hcr::DeviceIncarnationId::parse("inc.benchmark." + std::to_string(index));
    registration.identity.hardware.vendor = hcr::VendorId::parse("vendor.benchmark");
    registration.identity.hardware.product = hcr::ProductId::parse("product.benchmark");
    registration.identity.hardware.family = hcr::HardwareFamilyId::parse("family.benchmark");
    registration.identity.hardware.model = hcr::HardwareModelId::parse("model.benchmark");
    registration.identity.hardware.revision = hcr::HardwareRevisionId::parse("rev.benchmark.a0");
    registration.identity.hardware.architecture = hcr::ArchitectureId::parse("arch.benchmark");
    registration.identity.hardware.hardware_class = hcr::HardwareClass::Gpu;
    registration.identity.platform = hcr::PlatformId::parse("platform.benchmark");
    registration.identity.display_name = token;
    registration.software.driver.id = hcr::DriverId::parse("driver.benchmark");
    registration.software.driver.version = "1.0.0";
    registration.software.firmware.id = hcr::FirmwareId::parse("fw.benchmark");
    registration.software.firmware.version = "1.0.0";
    registration.platform.id = hcr::PlatformId::parse("platform.benchmark");

    hcr::Publication publication;
    publication.kind = hcr::PublicationKind::RegisterDevice;
    publication.publisher = fixture.publisher;
    publication.sequence = ++sequence;
    publication.evidence_generation = hcr::EvidenceGeneration::first();
    publication.device = registration;
    const hcr::ApplyResult applied = fixture.registry.apply(publication);
    fixture.devices.push_back(registration.identity.id);
    fixture.registrations.push_back(registration);
    if (!with_capabilities || !applied.accepted()) continue;
    for (const char* capability : kCapabilities) {
      hcr::Publication capability_publication;
      capability_publication.kind = hcr::PublicationKind::PublishCapability;
      capability_publication.publisher = fixture.publisher;
      capability_publication.sequence = ++sequence;
      capability_publication.evidence_generation = hcr::EvidenceGeneration::first();
      capability_publication.capability.device = registration.identity.id;
      capability_publication.capability.device_generation = applied.device_generation;
      capability_publication.capability.device_boot = applied.device_boot;
      capability_publication.capability.capability = hcr::CapabilityId::parse(capability);
      const hcr::CapabilitySchema* schema =
          hcr::find_builtin_schema_by_capability(capability_publication.capability.capability);
      capability_publication.capability.schema = schema->id;
      capability_publication.capability.support = hcr::SupportState::SupportedNative;
      if (std::string(capability) == "cap.memory.capacity") {
        capability_publication.capability.value = hcr::CapabilityValue(hcr::BytesValue{1024});
      } else {
        capability_publication.capability.value = hcr::CapabilityValue(true);
      }
      capability_publication.capability.precision = hcr::PrecisionClass::DirectReported;
      capability_publication.capability.source.source_class = hcr::SourceClass::LiveVendorApi;
      capability_publication.capability.source.provenance = hcr::ProvenanceClass::RealVendorApi;
      capability_publication.capability.source.adapter = "benchmark";
      capability_publication.capability.source.native_reference = "benchmark";
      fixture.registry.apply(capability_publication);
    }
  }
}

}  // namespace

int main() {
  std::printf("Hardware Capability Registry %s benchmarks\n\n", hcr::version_string());
  const std::vector<std::size_t> scales = {1, 100, 1000, 10000};

  for (const std::size_t scale : scales) {
    Fixture fixture;
    build_fleet(fixture, scale, true);

    // Device registration: one fresh device appended to the live registry.
    {
      const Measurement measurement = measure(5, 1, [&fixture]() {
        hcr::DeviceRegistration registration = fixture.registrations.front();
        registration.identity.id = hcr::canonical_device_id("dev.benchmark.extra", hcr::utc_now_iso8601());
        registration.identity.incarnation =
            hcr::DeviceIncarnationId::parse("inc.benchmark.extra." + hcr::hex64(hcr::fnv1a64(hcr::utc_now_iso8601())));
        hcr::Publication publication;
        publication.kind = hcr::PublicationKind::RegisterDevice;
        publication.publisher = fixture.publisher;
        publication.sequence = 900000000 + hcr::fnv1a64(hcr::utc_now_iso8601()) % 100000;
        publication.evidence_generation = hcr::EvidenceGeneration::first();
        publication.device = registration;
        fixture.registry.apply(publication);
      });
      report("register device", scale, measurement, 1);
    }

    // Capability publication against an existing device.
    {
      std::uint64_t sequence = 1000000;
      const Measurement measurement = measure(9, 1, [&fixture, &sequence]() {
        const std::size_t index = fixture.devices.size() / 2;
        hcr::Publication publication;
        publication.kind = hcr::PublicationKind::PublishCapability;
        publication.publisher = fixture.publisher;
        publication.sequence = ++sequence;
        publication.evidence_generation = hcr::EvidenceGeneration::first();
        publication.capability.device = fixture.devices[index];
        publication.capability.device_generation = hcr::DeviceGeneration::first();
        publication.capability.device_boot = hcr::DeviceBootId::first();
        publication.capability.capability = hcr::CapabilityId::parse("cap.compute.fp8");
        publication.capability.schema = hcr::CapabilitySchemaId::parse("cap.compute.fp8.v1");
        publication.capability.support = hcr::SupportState::SupportedNative;
        publication.capability.value = hcr::CapabilityValue(true);
        publication.capability.precision = hcr::PrecisionClass::DirectReported;
        publication.capability.source.source_class = hcr::SourceClass::LiveVendorApi;
        publication.capability.source.provenance = hcr::ProvenanceClass::RealVendorApi;
        publication.capability.source.adapter = "benchmark";
        fixture.registry.apply(publication);
      });
      report("publish capability", scale, measurement, 1);
    }

    // Capability query: one device, one capability, repeated.
    {
      const std::size_t repeats = 2000;
      const Measurement measurement = measure(5, repeats, [&fixture, repeats]() {
        for (std::size_t i = 0; i < repeats; ++i) {
          hcr::CapabilityQuery query;
          query.device = fixture.devices[i % fixture.devices.size()];
          query.capability = hcr::CapabilityId::parse(kCapabilities[i % 8]);
          hcr::ResolvedCapability resolved;
          fixture.registry.query_capability(query, resolved);
        }
      });
      report("query capability", scale, measurement, repeats);
    }

    // Full device capability listing.
    {
      const std::size_t repeats = 200;
      const Measurement measurement = measure(3, repeats, [&fixture, repeats]() {
        for (std::size_t i = 0; i < repeats; ++i) {
          hcr::DeviceCapabilityQuery query;
          query.device = fixture.devices[i % fixture.devices.size()];
          query.only_current = false;
          std::vector<hcr::ResolvedCapability> capabilities;
          fixture.registry.query_capabilities(query, capabilities);
        }
      });
      report("list device capabilities", scale, measurement, repeats);
    }

    // Fleet query: which devices support one capability.
    {
      const Measurement measurement = measure(3, 1, [&fixture]() {
        hcr::FleetCapabilityQuery query;
        query.capability = hcr::CapabilityId::parse("cap.memory.capacity");
        std::vector<hcr::ResolvedCapability> capabilities;
        fixture.registry.query_fleet(query, capabilities);
      });
      report("fleet query", scale, measurement, 1);
    }

    // Comparison of two devices.
    {
      const Measurement measurement = measure(20, 1, [&fixture]() {
        hcr::ComparisonRequest request;
        request.left = fixture.devices.front();
        request.right = fixture.devices.back();
        hcr::DeviceComparison comparison;
        fixture.registry.compare_devices(request, comparison);
      });
      report("compare two devices", scale, measurement, 1);
    }

    // Snapshot creation over the whole registry.
    {
      const Measurement measurement = measure(5, 1, [&fixture]() {
        hcr::RegistrySnapshot snapshot;
        fixture.registry.snapshot(snapshot);
      });
      report("snapshot", scale, measurement, 1);
    }

    // Persistence save and load (the file is written to the working directory).
    {
      const std::string path = "hcr-benchmark-state.bin";
      hcr::FileStateStore store(path);
      const Measurement saved = measure(3, 1, [&store, &fixture]() {
        store.save(fixture.registry.export_durable_state());
      });
      report("persistence save", scale, saved, 1);
      const Measurement loaded = measure(3, 1, [&store]() {
        hcr::DurableState state;
        store.load(state);
      });
      report("persistence load", scale, loaded, 1);
      std::remove(path.c_str());
    }

    // Quirk resolution on a device that has applicable quirks.
    {
      hcr::QuirkRecord quirk;
      quirk.id = hcr::QuirkId::parse("quirk.benchmark");
      quirk.quirk_generation = hcr::QuirkGeneration::first();
      quirk.title = "benchmark quirk";
      quirk.severity = hcr::QuirkSeverity::Caveat;
      quirk.applicability.families.push_back(hcr::HardwareFamilyId::parse("family.benchmark"));
      quirk.impacted_capabilities.push_back(hcr::CapabilityId::parse("cap.compute.fp32"));
      quirk.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
      quirk.source.provenance = hcr::ProvenanceClass::CuratedStatic;
      quirk.source.publisher.id = fixture.publisher.id;
      quirk.source.adapter = "benchmark";
      hcr::Publication publication;
      publication.kind = hcr::PublicationKind::PublishQuirk;
      publication.publisher = fixture.publisher;
      publication.sequence = 5000000;
      publication.evidence_generation = hcr::EvidenceGeneration::first();
      publication.quirk = quirk;
      fixture.registry.apply(publication);

      const std::size_t repeats = 500;
      const Measurement measurement = measure(3, repeats, [&fixture, repeats]() {
        for (std::size_t i = 0; i < repeats; ++i) {
          hcr::QuirkQuery query;
          query.device = fixture.devices[i % fixture.devices.size()];
          query.capability = hcr::CapabilityId::parse("cap.compute.fp32");
          std::vector<hcr::QuirkApplication> applications;
          fixture.registry.query_quirks(query, applications);
        }
      });
      report("quirk resolution", scale, measurement, repeats);
    }

    // Compatibility lookup.
    {
      hcr::CompatibilityFact fact;
      fact.id = hcr::CompatibilityFactId::parse("compat.benchmark");
      fact.compatibility_generation = hcr::CompatibilityGeneration::first();
      fact.kind = hcr::CompatibilitySubjectKind::AcceleratorRuntime;
      fact.lhs.kind = hcr::HardwareReference::Kind::HardwareFamily;
      fact.lhs.token = "family.benchmark";
      fact.rhs.kind = hcr::HardwareReference::Kind::Runtime;
      fact.rhs.token = "runtime.benchmark";
      fact.outcome = hcr::CompatibilityOutcome::Compatible;
      fact.reason = "benchmark fact";
      fact.source.source_class = hcr::SourceClass::StaticCuratedDatabase;
      fact.source.provenance = hcr::ProvenanceClass::CuratedStatic;
      fact.source.publisher.id = fixture.publisher.id;
      fact.source.adapter = "benchmark";
      hcr::Publication publication;
      publication.kind = hcr::PublicationKind::PublishCompatibility;
      publication.publisher = fixture.publisher;
      publication.sequence = 5000001;
      publication.evidence_generation = hcr::EvidenceGeneration::first();
      publication.compatibility = fact;
      fixture.registry.apply(publication);

      const std::size_t repeats = 500;
      const Measurement measurement = measure(3, repeats, [&fixture, repeats]() {
        for (std::size_t i = 0; i < repeats; ++i) {
          hcr::CompatibilityQuery query;
          hcr::HardwareReference reference;
          reference.kind = hcr::HardwareReference::Kind::HardwareFamily;
          reference.token = "family.benchmark";
          query.reference = reference;
          std::vector<hcr::CompatibilityFact> facts;
          fixture.registry.query_compatibility(query, facts);
        }
      });
      report("compatibility lookup", scale, measurement, repeats);
    }

    std::printf("\n");
  }

  // Synthetic fleet ingestion through the same production pipeline.
  {
    hcr::CapabilityRegistry registry;
    hcr::LocalEvidenceSink sink(registry);
    hcr::DiscoveryContext context;
    context.publisher.id = hcr::PublisherId::parse("publisher.benchmark.synthetic");
    context.platform.id = hcr::PlatformId::parse("platform.benchmark");
    context.observed_at_utc = hcr::utc_now_iso8601();
    const Measurement measurement = measure(5, 1, [&registry, &sink, &context]() {
      auto backend = hcr::adapters::make_synthetic_backend(hcr::SyntheticProfile::MixedVendorFleet);
      hcr::DiscoveryReport report;
      hcr::run_discovery(*backend, context, sink, report);
    });
    report("synthetic fleet ingestion", 7, measurement, 1);
  }

  std::printf("\n%s\n", hcr::render_resource_usage(hcr::CapabilityRegistry{}.resource_usage()).c_str());
  return 0;
}
