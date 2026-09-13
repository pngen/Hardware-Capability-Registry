// Hardware Capability Registry - real hardware validation tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// These tests exercise the real discovery adapters on the machine they run on.
// They assert only what the adapters are allowed to claim: a fact that was not
// proven must be absent or UNKNOWN, never SUPPORTED. Adapters that cannot run
// are reported as skips with the reason the adapter itself gives.
#include <algorithm>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/adapters.hpp"
#include "hcr/catalog.hpp"
#include "hcr/discovery.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"

using namespace hcr;
using namespace hcr::adapters;
using namespace hcrtest;

namespace {

struct HardwareRun {
  CapabilityRegistry registry;
  std::vector<DiscoveryReport> reports;
  PlatformIdentity platform;
  DiscoveryContext context;
  DeviceId host;

  bool ran(const std::string& adapter) const {
    for (const DiscoveryReport& report : reports) {
      if (report.adapter == adapter) return report.available;
    }
    return false;
  }

  const DiscoveryReport* report_for(const std::string& adapter) const {
    for (const DiscoveryReport& report : reports) {
      if (report.adapter == adapter) return &report;
    }
    return nullptr;
  }

  SubjectSnapshot subject(const std::string& token) const {
    SubjectSnapshot snapshot;
    const Status status = registry.find_device(canonical_device_id("dev.synthetic", token), snapshot);
    if (!status.ok()) throw hcrtest::Failure("synthetic subject missing: " + token);
    return snapshot;
  }

  ResolvedCapability query(const DeviceId& device, const char* capability) const {
    CapabilityQuery request;
    request.device = device;
    request.capability = CapabilityId::parse(capability);
    ResolvedCapability resolved;
    const Status status = registry.query_capability(request, resolved);
    if (!status.ok()) throw hcrtest::Failure(status.describe());
    return resolved;
  }
};

void run_real_adapters(HardwareRun& run) {
  EnvironmentContext environment;
  DeviceSoftwareState software;
  std::string detail;
  const Status probed = probe_host_platform(run.platform, environment, software, detail);
  HCR_CHECK_MSG(probed.ok(), probed.describe());
  run.host = host_device_id(run.platform);

  run.context.publisher.id = PublisherId::parse("publisher.hardware-tests");
  run.context.platform = run.platform;
  run.context.software = software;
  run.context.environment = environment;
  run.context.observed_at_utc = utc_now_iso8601();

  LocalEvidenceSink sink(run.registry);
  std::vector<std::unique_ptr<IDiscoveryBackend>> backends;
  backends.push_back(make_cpu_platform_backend());
  backends.push_back(make_pci_topology_backend());
  backends.push_back(make_nvml_backend());
  backends.push_back(make_cuda_runtime_backend());
  backends.push_back(make_network_backend());
  backends.push_back(make_cxl_backend());
  for (const auto& backend : backends) {
    DiscoveryReport report;
    HCR_CHECK_STATUS(run_discovery(*backend, run.context, sink, report));
    run.reports.push_back(report);
  }
}

}  // namespace

HCR_TEST(hardware, cpu_and_platform_facts_are_real) {
  HardwareRun run;
  run_real_adapters(run);
  HCR_REQUIRE(run.ran("cpu.platform"));

  SubjectSnapshot cpu;
  bool found = false;
  std::vector<SubjectSnapshot> subjects;
  HCR_CHECK_STATUS(run.registry.list_devices(subjects));
  for (const SubjectSnapshot& subject : subjects) {
    if (subject.identity.hardware.hardware_class == HardwareClass::Cpu) {
      cpu = subject;
      found = true;
    }
  }
  HCR_CHECK(found);
  HCR_CHECK(cpu.identity.hardware.vendor.valid());

  const ResolvedCapability logical =
      run.query(cpu.identity.id, "cap.platform.cpu.logical_processors");
  HCR_CHECK(logical.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(logical.truth == TruthClass::Real);
  HCR_REQUIRE(logical.value.kind() == ValueKind::Count);
  const std::uint64_t logical_count = std::get<CountValue>(logical.value.storage()).count;
  HCR_CHECK(logical_count >= 1u);

  const ResolvedCapability cores = run.query(cpu.identity.id, "cap.platform.cpu.physical_cores");
  HCR_CHECK(cores.effective_support == EffectiveSupport::Supported);
  const std::uint64_t core_count = std::get<CountValue>(cores.value.storage()).count;
  HCR_CHECK(core_count >= 1u);
  HCR_CHECK(logical_count >= core_count);

  const ResolvedCapability isa = run.query(cpu.identity.id, "cap.platform.cpu.isa_features");
  HCR_CHECK(isa.effective_support == EffectiveSupport::Supported);
  HCR_REQUIRE(isa.value.kind() == ValueKind::FeatureSet);
  HCR_CHECK(!std::get<FeatureSetValue>(isa.value.storage()).features.empty());

  const ResolvedCapability os = run.query(run.host, "cap.platform.os");
  HCR_CHECK(os.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(os.truth == TruthClass::Real);

  const ResolvedCapability pci = run.query(run.host, "cap.platform.pci_devices");
  HCR_REQUIRE(run.ran("pci.topology"));
  HCR_CHECK(pci.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(std::get<CountValue>(pci.value.storage()).count > 0u);
}

HCR_TEST(hardware, nvidia_gpu_facts_match_what_the_vendor_api_reports) {
  HardwareRun run;
  run_real_adapters(run);
  if (!run.ran("nvml")) {
    const DiscoveryReport* report = run.report_for("nvml");
    HCR_SKIP(report != nullptr && !report->notes.empty() ? report->notes.front()
                                                         : "NVML is not available on this host");
  }

  std::vector<SubjectSnapshot> subjects;
  HCR_CHECK_STATUS(run.registry.list_devices(subjects));
  std::vector<SubjectSnapshot> gpus;
  for (const SubjectSnapshot& subject : subjects) {
    if (subject.identity.hardware.hardware_class == HardwareClass::Gpu &&
        subject.identity.hardware.vendor == VendorId::parse("vendor.nvidia")) {
      gpus.push_back(subject);
    }
  }
  HCR_REQUIRE(!gpus.empty());
  const SubjectSnapshot& gpu = gpus.front();

  const ResolvedCapability capability = run.query(gpu.identity.id, "cap.compute.capability");
  HCR_CHECK(capability.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(capability.truth == TruthClass::Real);
  HCR_CHECK(capability.precision == PrecisionClass::DirectReported);

  const ResolvedCapability memory = run.query(gpu.identity.id, "cap.memory.capacity");
  HCR_CHECK(memory.effective_support == EffectiveSupport::Supported);
  HCR_CHECK(std::get<BytesValue>(memory.value.storage()).bytes > 0u);

  // MIG must be reported exactly as the hardware reports it. On a device whose
  // NVML returns NOT_SUPPORTED, the registry publishes UNSUPPORTED for this
  // exact device generation and never infers family-wide support.
  const ResolvedCapability mig = run.query(gpu.identity.id, "cap.partition.mechanism");
  HCR_CHECK(mig.effective_support == EffectiveSupport::Unsupported ||
            mig.effective_support == EffectiveSupport::Supported ||
            mig.effective_support == EffectiveSupport::Disabled);
  if (mig.effective_support == EffectiveSupport::Unsupported) {
    HCR_CHECK(mig.declared_support == SupportState::Unsupported);
    HCR_CHECK(mig.truth == TruthClass::Unsupported);
    HCR_CHECK(mig.precision == PrecisionClass::Exact);
    HCR_CHECK(mig.explanation.find("nvmlDeviceGetMigMode") != std::string::npos);
  }
  HCR_CHECK(gpu.identity.hardware.architecture.valid());

  // Both live NVIDIA sources must resolve to exactly one canonical subject.
  {
    std::size_t nvidia_gpus = 0;
    for (const SubjectSnapshot& subject : subjects) {
      if (subject.identity.hardware.hardware_class == HardwareClass::Gpu &&
          subject.identity.hardware.vendor == VendorId::parse("vendor.nvidia")) {
        ++nvidia_gpus;
      }
    }
    HCR_CHECK_EQ(nvidia_gpus, std::size_t{1});
  }

  if (run.ran("cuda.driver-api")) {
    // The CUDA driver API resolves the same vendor UUID to the same canonical
    // identity, so the two live sources contribute to one subject instead of
    // creating two.
    const ResolvedCapability multiprocessors =
        run.query(gpu.identity.id, "cap.compute.multiprocessors");
    HCR_CHECK(multiprocessors.effective_support == EffectiveSupport::Supported);
    HCR_CHECK_EQ(multiprocessors.contradiction, ContradictionState::Consistent);
    HCR_CHECK(multiprocessors.evidence.front().source.source_class == SourceClass::CudaRuntime);
    const ResolvedCapability curated = run.query(gpu.identity.id, "cap.compute.datatypes");
    HCR_CHECK(curated.precision == PrecisionClass::Curated);
    HCR_CHECK(curated.declared_support == SupportState::SupportedNative);
  }
}

HCR_TEST(hardware, network_adapters_publish_only_discoverable_facts) {
  HardwareRun run;
  run_real_adapters(run);
  if (!run.ran("nic.windows")) {
    HCR_SKIP("the network adapter backend is not available on this host");
  }
  std::vector<SubjectSnapshot> subjects;
  HCR_CHECK_STATUS(run.registry.list_devices(subjects));
  std::vector<SubjectSnapshot> nics;
  for (const SubjectSnapshot& subject : subjects) {
    if (subject.identity.hardware.hardware_class == HardwareClass::Nic) nics.push_back(subject);
  }
  if (nics.empty()) HCR_SKIP("no physical network interface was enumerated on this host");

  for (const SubjectSnapshot& nic : nics) {
    const ResolvedCapability state = run.query(nic.identity.id, "cap.net.interface_state");
    HCR_CHECK(state.effective_support == EffectiveSupport::Supported);
    HCR_CHECK(state.truth == TruthClass::Real);
    // RDMA is not discoverable through this API and therefore must not be
    // claimed in either direction.
    const ResolvedCapability rdma = run.query(nic.identity.id, "cap.net.rdma");
    HCR_CHECK(!rdma.known);
    HCR_CHECK(rdma.effective_support == EffectiveSupport::Unknown);
  }
}

HCR_TEST(hardware, cxl_absence_is_reported_honestly) {
  HardwareRun run;
  run_real_adapters(run);
  HCR_REQUIRE(run.ran("cxl.enumeration"));
  const ResolvedCapability count = run.query(run.host, "cap.platform.cxl_devices");
  HCR_CHECK(count.effective_support == EffectiveSupport::Supported);
  const std::uint64_t devices = std::get<CountValue>(count.value.storage()).count;

  const ResolvedCapability exposure = run.query(run.host, "cap.memory.cxl_exposure");
  if (devices == 0) {
    HCR_CHECK(exposure.declared_support == SupportState::Unsupported);
    HCR_CHECK(exposure.truth == TruthClass::Unsupported);
    HCR_CHECK(exposure.effective_support == EffectiveSupport::Unsupported);
  } else {
    HCR_CHECK(exposure.effective_support == EffectiveSupport::Supported);
  }

  // SmartNIC/DPU processing is not claimed on ordinary platform hardware: the
  // registry has no evidence, and absence of evidence is UNKNOWN.
  const ResolvedCapability smartnic = run.query(run.host, "cap.net.smartnic_processing");
  HCR_CHECK(smartnic.effective_support == EffectiveSupport::Unknown);
  HCR_CHECK(smartnic.truth == TruthClass::Unknown);
}

HCR_TEST(hardware, synthetic_evidence_is_classified_synthetic_and_never_outranks_real) {
  CapabilityRegistry registry;
  LocalEvidenceSink sink(registry);
  PlatformIdentity platform;
  EnvironmentContext environment;
  DeviceSoftwareState software;
  std::string detail;
  HCR_CHECK_STATUS(probe_host_platform(platform, environment, software, detail));

  DiscoveryContext context;
  context.publisher.id = PublisherId::parse("publisher.synthetic-tests");
  context.platform = platform;
  context.software = software;
  context.environment = environment;
  context.observed_at_utc = utc_now_iso8601();

  DiscoveryReport report;
  auto synthetic = make_synthetic_backend(SyntheticProfile::MixedVendorFleet);
  HCR_CHECK_STATUS(run_discovery(*synthetic, context, sink, report));
  HCR_CHECK(report.accepted > 0u);

  std::vector<SubjectSnapshot> subjects;
  HCR_CHECK_STATUS(registry.list_devices(subjects));
  HCR_CHECK(!subjects.empty());
  for (const SubjectSnapshot& subject : subjects) {
    HCR_CHECK(subject.classification == TruthClass::Synthetic ||
              subject.classification == TruthClass::Unsupported ||
              subject.classification == TruthClass::Unknown);
  }

  DeviceCapabilityQuery query;
  query.device = canonical_device_id("dev.synthetic", "h200");
  query.only_current = false;
  std::vector<ResolvedCapability> capabilities;
  HCR_CHECK_STATUS(registry.query_capabilities(query, capabilities));
  HCR_CHECK(!capabilities.empty());
  for (const ResolvedCapability& capability : capabilities) {
    for (const EvidenceView& view : capability.evidence) {
      if (!view.winner) continue;
      const bool synthetic_source = view.source.provenance == ProvenanceClass::Synthetic;
      HCR_CHECK(!synthetic_source || capability.truth == TruthClass::Synthetic ||
                capability.truth == TruthClass::Unsupported);
      HCR_CHECK(!view.source.live());
    }
  }
}

HCR_TEST(hardware, real_evidence_is_classified_real) {
  HardwareRun run;
  run_real_adapters(run);
  HCR_REQUIRE(run.ran("cpu.platform"));
  const ResolvedCapability os = run.query(run.host, "cap.platform.os");
  HCR_CHECK(os.truth == TruthClass::Real);
  HCR_CHECK(os.evidence.front().source.provenance == ProvenanceClass::RealOsReported);
  RegistrySnapshot snapshot;
  HCR_CHECK_STATUS(run.registry.snapshot(snapshot));
  HCR_CHECK(snapshot.real_statements > 0u);
}
