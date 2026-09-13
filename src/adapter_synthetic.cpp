// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Deterministic synthetic fleet. Every publication travels through the same
// production ingestion path as real hardware evidence and is classified
// SYNTHETIC. Synthetic evidence never outranks real evidence.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "adapter_common.hpp"
#include "hcr/adapters.hpp"

namespace hcr {
namespace {

using adapter_detail::all_of;
using adapter_detail::cap;
using adapter_detail::canonical_token;
using adapter_detail::condition;
using adapter_detail::count;
using adapter_detail::enumeration;
using adapter_detail::features;

CapabilityValue boolean(bool value) { return CapabilityValue(value); }

CapabilityValue bytes_value(std::uint64_t value) { return CapabilityValue(adapter_detail::bytes(value)); }

CapabilityValue count_value(std::uint64_t value, const char* unit = "") {
  return CapabilityValue(count(value, unit));
}

CapabilityValue enum_value(const char* domain, const char* value) {
  return CapabilityValue(enumeration(domain, value));
}

CapabilityValue features_value(std::vector<std::string> values) {
  return CapabilityValue(features(std::move(values)));
}

CapabilityValue architectures_value(std::vector<std::string> values) {
  ArchitectureSetValue set;
  for (const std::string& value : values) set.architectures.push_back(ArchitectureId::parse(value));
  std::sort(set.architectures.begin(), set.architectures.end());
  return CapabilityValue(set);
}

CapabilityValue bandwidth_value(std::uint64_t magnitude, BandwidthUnit unit) {
  BandwidthValue value;
  value.magnitude = magnitude;
  value.unit = unit;
  return CapabilityValue(value);
}

CapabilityValue split_bandwidth(std::uint64_t gigabytes_per_second) {
  BandwidthValue value;
  value.magnitude = gigabytes_per_second;
  value.unit = BandwidthUnit::GigabytesPerSecond;
  return CapabilityValue(value);
}

struct SyntheticCapability {
  const char* capability = "";
  SupportState support = SupportState::SupportedNative;
  CapabilityValue value;
  PrecisionClass precision = PrecisionClass::DirectReported;
  const char* reference = "synthetic.backend";
  ConditionPtr conditions;
  SourceClass source_class = SourceClass::Unspecified;
  ProvenanceClass provenance = ProvenanceClass::UnknownSource;
};

struct SyntheticDevice {
  std::string vendor;
  std::string family;
  std::string model;
  std::string architecture;
  std::string revision;
  std::string token;
  std::string display;
  HardwareClass hardware_class = HardwareClass::Gpu;
  std::string driver_id;
  std::string driver_version;
  std::string firmware_id;
  std::string firmware_version;
  std::string runtime_id;
  std::string runtime_version;
  std::vector<SyntheticCapability> capabilities;
};

struct SyntheticFleet {
  std::vector<SyntheticDevice> devices;
  std::vector<QuirkRecord> quirks;
  std::vector<CompatibilityFact> compatibility;
  /// Devices that reincarnate mid-run: index plus the capability set published
  /// after the reset.
  std::vector<std::pair<std::size_t, std::vector<SyntheticCapability>>> reincarnations;
  std::vector<std::string> notes;
};

void add(SyntheticDevice& device, const char* capability, SupportState support, CapabilityValue value,
         PrecisionClass precision, const char* reference, ConditionPtr conditions = nullptr) {
  SyntheticCapability entry;
  entry.capability = capability;
  entry.support = support;
  entry.value = std::move(value);
  entry.precision = precision;
  entry.reference = reference;
  entry.conditions = std::move(conditions);
  device.capabilities.push_back(std::move(entry));
}

void add_classified(SyntheticDevice& device, const char* capability, SupportState support,
                    CapabilityValue value, PrecisionClass precision, const char* reference,
                    SourceClass source_class, ProvenanceClass provenance) {
  SyntheticCapability entry;
  entry.capability = capability;
  entry.support = support;
  entry.value = std::move(value);
  entry.precision = precision;
  entry.reference = reference;
  entry.source_class = source_class;
  entry.provenance = provenance;
  device.capabilities.push_back(std::move(entry));
}

SyntheticDevice make_gpu(std::string vendor, std::string family, std::string model, std::string architecture,
                         std::string driver_version, std::string firmware_version,
                         std::string runtime_version, std::string token) {
  SyntheticDevice device;
  device.vendor = std::move(vendor);
  device.family = std::move(family);
  device.model = std::move(model);
  device.architecture = std::move(architecture);
  device.token = std::move(token);
  device.display = device.model;
  device.hardware_class = HardwareClass::Gpu;
  device.driver_id = "driver." + device.vendor;
  device.driver_version = std::move(driver_version);
  device.firmware_id = "fw." + device.vendor;
  device.firmware_version = std::move(firmware_version);
  device.runtime_id = "runtime." + device.vendor;
  device.runtime_version = std::move(runtime_version);
  device.revision = "rev." + device.architecture + ".a0";
  return device;
}

SyntheticFleet make_mixed_vendor_fleet() {
  SyntheticFleet fleet;
  {
    SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.datacenter", "model.nvidia.h200",
                                      "arch.nvidia.sm_90", "570.86.15", "96.00.74.00.01", "12.8", "h200");
    add(device, "cap.compute.fp64", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.nvml");
    add(device, "cap.compute.fp8", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.nvml");
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(141000000000ull),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.memory.type", SupportState::SupportedNative, enum_value("memory_type", "hbm3"),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.partition.mechanism", SupportState::SupportedNative,
        features_value({"mig", "sriov"}), PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.partition.max_partitions", SupportState::SupportedNative, count_value(7, "partitions"),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.interconnect.nvlink", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.net.gpudirect", SupportState::SupportedConditional, boolean(true),
        PrecisionClass::DirectReported, "synthetic.nvml",
        condition(ConditionKind::MinDriverVersion, "", "550.0"));
    add(device, "cap.reliability.ecc_exposure", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.runtime.cuda", SupportState::SupportedNative, enum_value("runtime", "12.8"),
        PrecisionClass::DirectReported, "synthetic.runtime");
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.consumer", "model.nvidia.rtx5090",
                                      "arch.nvidia.sm_120", "616.92", "98.02.5c.00.0a", "12.9", "rtx5090");
    add(device, "cap.compute.fp32", SupportState::SupportedNative, boolean(true), PrecisionClass::Probed,
        "synthetic.runtime");
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(34251081216ull),
        PrecisionClass::DirectReported, "synthetic.nvml");
    add(device, "cap.memory.type", SupportState::SupportedNative, enum_value("memory_type", "gddr7"),
        PrecisionClass::DirectReported, "synthetic.nvml");
    // The registry must not invent family-wide MIG support for a consumer part.
    add(device, "cap.partition.mechanism", SupportState::Unsupported, CapabilityValue{},
        PrecisionClass::Exact, "synthetic.nvmlGetMigMode");
    add(device, "cap.net.gpudirect", SupportState::Unknown, CapabilityValue{}, PrecisionClass::Unknown,
        "synthetic.none");
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.amd", "family.amd.instinct", "model.amd.mi300x",
                                      "arch.amd.cdna3", "6.10.5", "0x0", "6.2", "mi300x");
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(192000000000ull),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.memory.coherence", SupportState::SupportedNative,
        enum_value("coherence", "system-wide"), PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.compute.fp64", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.runtime.rocm", SupportState::SupportedNative, enum_value("runtime", "6.2"),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.interconnect.infinity_fabric", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.intel", "family.intel.gaudi", "model.intel.gaudi3",
                                      "arch.intel.gaudi3", "1.15.0", "0x1", "1.15", "gaudi3");
    device.hardware_class = HardwareClass::AsicAccelerator;
    add(device, "cap.compute.bf16", SupportState::SupportedNative, boolean(true), PrecisionClass::Curated,
        "curated.architecture-table");
    add(device, "cap.compute.fp8", SupportState::SupportedNative, boolean(true), PrecisionClass::Curated,
        "curated.architecture-table");
    add(device, "cap.interconnect.collective_offload", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.vendor-api");
    add(device, "cap.net.rdma", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.vendor-api");
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.dpu", "model.nvidia.bluefield3",
                                      "arch.nvidia.bf3", "24.10", "32.42.1000", "4.7", "bluefield3");
    device.hardware_class = HardwareClass::Dpu;
    add(device, "cap.net.rdma", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.nic-api");
    add(device, "cap.net.roce", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.nic-api");
    add(device, "cap.net.smartnic_processing", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.nic-api");
    add(device, "cap.net.sriov", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
        "synthetic.nic-api");
    add(device, "cap.net.encryption_offload", SupportState::SupportedConditional, boolean(true),
        PrecisionClass::DirectReported, "synthetic.nic-api",
        condition(ConditionKind::RequiredPrivilege, "", "admin"));
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.microchip", "family.cxl.memory", "model.cxl.memory-expander",
                                      "arch.cxl.3.0", "1.4.0", "1.0.0", "1.4", "cxl-expander");
    device.hardware_class = HardwareClass::CxlDevice;
    add(device, "cap.memory.cxl_exposure", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(512000000000ull),
        PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
    add(device, "cap.memory.coherence", SupportState::SupportedNative,
        enum_value("coherence", "coherent"), PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
    add(device, "cap.interconnect.cxl", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
    add(device, "cap.memory.page_sizes", SupportState::SupportedNative, features_value({"4k", "2m"}),
        PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
    fleet.devices.push_back(std::move(device));
  }
  {
    SyntheticDevice device = make_gpu("vendor.intel", "family.intel.gpu", "model.intel.arc.b580",
                                      "arch.intel.xe2", "32.0.101", "1.0", "1.6", "arc-b580");
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(12884901888ull),
        PrecisionClass::DirectReported, "synthetic.level-zero");
    add(device, "cap.runtime.level_zero", SupportState::SupportedNative, enum_value("runtime", "1.6"),
        PrecisionClass::DirectReported, "synthetic.level-zero");
    add(device, "cap.net.gpudirect", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
        "synthetic.peer-query");
    add(device, "cap.compute.fp64", SupportState::SupportedEmulated, boolean(true), PrecisionClass::Curated,
        "curated.architecture-table");
    fleet.devices.push_back(std::move(device));
  }
  return fleet;
}

SyntheticFleet make_generation_drift() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.consumer", "model.nvidia.rtx5090",
                                    "arch.nvidia.sm_120", "610.10", "98.02.5c.00.0a", "12.8", "drift-gpu");
  add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(34251081216ull),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.runtime.external_memory_handles", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.runtime");
  fleet.devices.push_back(device);

  std::vector<SyntheticCapability> after;
  {
    SyntheticCapability entry;
    entry.capability = "cap.runtime.external_memory_handles";
    entry.support = SupportState::SupportedNative;
    entry.value = boolean(true);
    entry.precision = PrecisionClass::DirectReported;
    entry.reference = "synthetic.runtime";
    after.push_back(entry);
  }
  {
    SyntheticCapability entry;
    entry.capability = "cap.runtime.graph_capture";
    entry.support = SupportState::SupportedNative;
    entry.value = boolean(true);
    entry.precision = PrecisionClass::DirectReported;
    entry.reference = "synthetic.runtime";
    after.push_back(entry);
  }
  fleet.reincarnations.emplace_back(0u, after);
  return fleet;
}

SyntheticFleet make_amd_fleet() {
  SyntheticFleet fleet;
  for (int index = 0; index < 3; ++index) {
    SyntheticDevice device = make_gpu("vendor.amd", "family.amd.instinct", "model.amd.mi250x",
                                      "arch.amd.cdna3", "6.4.0", "0x2", "6.0",
                                      "mi250x." + std::to_string(index));
    device.token = "mi250x-" + std::to_string(index);
    add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(128000000000ull),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.memory.bandwidth", SupportState::SupportedNative,
        split_bandwidth(3276), PrecisionClass::Curated, "curated.architecture-table");
    add(device, "cap.compute.fp64", SupportState::SupportedNative, boolean(true),
        PrecisionClass::DirectReported, "synthetic.rocm-smi");
    add(device, "cap.interconnect.peer_to_peer", SupportState::SupportedConditional, boolean(true),
        PrecisionClass::DirectReported, "synthetic.rocm-smi",
        condition(ConditionKind::RequiredTopology, "", "xgmi"));
    fleet.devices.push_back(std::move(device));
  }
  CompatibilityFact fact;
  fact.id = CompatibilityFactId::parse("compat.amd.mi250x.rocm");
  fact.compatibility_generation = CompatibilityGeneration::first();
  fact.kind = CompatibilitySubjectKind::AcceleratorRuntime;
  fact.lhs.kind = HardwareReference::Kind::HardwareFamily;
  fact.lhs.token = "family.amd.instinct";
  fact.rhs.kind = HardwareReference::Kind::Runtime;
  fact.rhs.token = "runtime.rocm";
  fact.rhs.version = SoftwareVersion::parse("6.0");
  fact.outcome = CompatibilityOutcome::CompatibleConditional;
  fact.reason = "requires the ROCm 6.0 runtime or newer for the full capability set";
  fact.version_window.minimum = SoftwareVersion::parse("6.0");
  fact.source.source_class = SourceClass::SyntheticBackend;
  fact.source.provenance = ProvenanceClass::Synthetic;
  fact.source.publisher.id = PublisherId::parse("publisher.synthetic");
  fact.source.adapter = "synthetic.backend";
  fleet.compatibility.push_back(std::move(fact));
  return fleet;
}

SyntheticFleet make_cxl_fabric() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.microchip", "family.cxl.memory", "model.cxl.memory-expander",
                                    "arch.cxl.3.0", "1.4.0", "1.0.0", "1.4", "cxl-1");
  device.hardware_class = HardwareClass::CxlDevice;
  add(device, "cap.memory.cxl_exposure", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
  add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(256000000000ull),
      PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
  add(device, "cap.interconnect.cxl", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
  fleet.devices.push_back(device);

  SyntheticDevice constrained = make_gpu("vendor.microchip", "family.cxl.memory",
                                         "model.cxl.memory-expander.legacy", "arch.cxl.2.0", "1.1.0", "0.9.0",
                                         "1.1", "cxl-2");
  constrained.hardware_class = HardwareClass::MemoryExpander;
  add(constrained, "cap.memory.cxl_exposure", SupportState::SupportedConditional, boolean(true),
      PrecisionClass::DirectReported, "synthetic.cxl-enumeration",
      condition(ConditionKind::RequiredPlatformSetting, "bios.cxl", "enabled"));
  add(constrained, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(128000000000ull),
      PrecisionClass::DirectReported, "synthetic.cxl-enumeration");
  fleet.devices.push_back(constrained);

  CompatibilityFact fact;
  fact.id = CompatibilityFactId::parse("compat.cxl.expander.platform");
  fact.compatibility_generation = CompatibilityGeneration::first();
  fact.kind = CompatibilitySubjectKind::CxlPlatform;
  fact.lhs.kind = HardwareReference::Kind::Device;
  fact.lhs.token = canonical_device_id("dev.synthetic", "cxl-2").str();
  fact.rhs.kind = HardwareReference::Kind::Architecture;
  fact.rhs.token = "arch.cxl.2.0";
  fact.outcome = CompatibilityOutcome::CompatibleConditional;
  fact.reason = "CXL 2.0 expander requires the platform BIOS to enable CXL";
  fact.conditions = condition(ConditionKind::RequiredPlatformSetting, "bios.cxl", "enabled");
  fact.source.source_class = SourceClass::SyntheticBackend;
  fact.source.provenance = ProvenanceClass::Synthetic;
  fact.source.publisher.id = PublisherId::parse("publisher.synthetic");
  fact.source.adapter = "synthetic.backend";
  fleet.compatibility.push_back(std::move(fact));
  return fleet;
}

SyntheticFleet make_dpu_fabric() {
  SyntheticFleet fleet;
  SyntheticDevice dpu = make_gpu("vendor.nvidia", "family.nvidia.dpu", "model.nvidia.bluefield3",
                                 "arch.nvidia.bf3", "24.10", "32.42.1000", "4.7", "bf3-0");
  dpu.hardware_class = HardwareClass::Dpu;
  add(dpu, "cap.net.rdma", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  add(dpu, "cap.net.roce", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  add(dpu, "cap.net.queue_pairs", SupportState::SupportedNative, count_value(2048, "queues"),
      PrecisionClass::DirectReported, "synthetic.nic-api");
  add(dpu, "cap.net.smartnic_processing", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.nic-api");
  add(dpu, "cap.net.flow_steering", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  fleet.devices.push_back(dpu);

  SyntheticDevice nic = make_gpu("vendor.mellanox", "family.mellanox.connectx", "model.mellanox.cx7",
                                 "arch.mellanox.cx7", "24.10", "16.35.2000", "4.7", "cx7-0");
  nic.hardware_class = HardwareClass::Nic;
  add(nic, "cap.net.rdma", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  add(nic, "cap.net.link_speed", SupportState::SupportedNative,
      CapabilityValue(adapter_detail::bits_per_second(400000000000ull)), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  add(nic, "cap.net.sriov", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.nic-api");
  add(nic, "cap.net.smartnic_processing", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
      "synthetic.nic-api");
  fleet.devices.push_back(nic);

  CompatibilityFact fact;
  fact.id = CompatibilityFactId::parse("compat.gpu.nic.direct-path");
  fact.compatibility_generation = CompatibilityGeneration::first();
  fact.kind = CompatibilitySubjectKind::GpuNicDirectPath;
  fact.lhs.kind = HardwareReference::Kind::HardwareFamily;
  fact.lhs.token = "family.nvidia.dpu";
  fact.rhs.kind = HardwareReference::Kind::HardwareFamily;
  fact.rhs.token = "family.mellanox.connectx";
  fact.outcome = CompatibilityOutcome::Compatible;
  fact.reason = "peer memory path between the DPU and the ConnectX-class adapter is supported";
  fact.source.source_class = SourceClass::SyntheticBackend;
  fact.source.provenance = ProvenanceClass::Synthetic;
  fact.source.publisher.id = PublisherId::parse("publisher.synthetic");
  fact.source.adapter = "synthetic.backend";
  fleet.compatibility.push_back(std::move(fact));
  return fleet;
}

SyntheticFleet make_partitioning() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.datacenter", "model.nvidia.h100",
                                    "arch.nvidia.sm_90", "550.54", "92.00.3f.00.0a", "12.4", "h100-mig");
  add(device, "cap.partition.mechanism", SupportState::SupportedNative, features_value({"mig"}),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.partition.profiles", SupportState::SupportedNative,
      features_value({"1g.10gb", "2g.20gb", "3g.40gb", "7g.80gb"}), PrecisionClass::DirectReported,
      "synthetic.nvml");
  add(device, "cap.partition.max_partitions", SupportState::SupportedNative, count_value(7, "partitions"),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.partition.memory_isolation", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.partition.live_reconfiguration", SupportState::SupportedConditional,
      enum_value("live_reconfiguration", "reset-preferred"), PrecisionClass::DirectReported, "synthetic.nvml",
      condition(ConditionKind::RequiredPrivilege, "", "admin"));
  add(device, "cap.partition.reset_required", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.nvml");
  fleet.devices.push_back(device);

  SyntheticDevice consumer = make_gpu("vendor.amd", "family.amd.radeon", "model.amd.radeon.rx7900",
                                      "arch.amd.rdna3", "6.4.0", "0x3", "6.0", "rx7900");
  add(consumer, "cap.partition.mechanism", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
      "synthetic.rocm-smi");
  add(consumer, "cap.partition.max_partitions", SupportState::Unsupported, CapabilityValue{},
      PrecisionClass::Exact, "synthetic.rocm-smi");
  fleet.devices.push_back(consumer);
  return fleet;
}

SyntheticFleet make_contradiction() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.datacenter", "model.nvidia.a100",
                                    "arch.nvidia.sm_80", "470.57", "92.00.02.00.01", "11.4", "a100-conflict");
  // A curated database claims the capability; a live probe reports it
  // unsupported under this driver. Both records are preserved and the resolver
  // must explain which one won.
  add_classified(device, "cap.interconnect.peer_to_peer", SupportState::SupportedNative, boolean(true),
                 PrecisionClass::Curated, "curated.architecture-table", SourceClass::StaticCuratedDatabase,
                 ProvenanceClass::CuratedStatic);
  add_classified(device, "cap.compute.fp8", SupportState::SupportedNative, boolean(true),
                 PrecisionClass::Curated, "curated.architecture-table", SourceClass::StaticCuratedDatabase,
                 ProvenanceClass::CuratedStatic);
  add_classified(device, "cap.compute.fp8", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
                 "synthetic.runtime-probe", SourceClass::RuntimeProbe, ProvenanceClass::RealRuntimeProbe);
  add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(42949672960ull),
      PrecisionClass::DirectReported, "synthetic.nvml");
  fleet.devices.push_back(device);
  return fleet;
}

SyntheticFleet make_quirk_scenario() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.datacenter", "model.nvidia.h100",
                                    "arch.nvidia.sm_90", "535.104", "92.00.3f.00.0a", "12.2", "h100-quirk");
  add(device, "cap.interconnect.peer_to_peer", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.runtime.external_memory_handles", SupportState::SupportedNative, boolean(true),
      PrecisionClass::DirectReported, "synthetic.runtime");
  fleet.devices.push_back(device);

  QuirkRecord driver_defect;
  driver_defect.id = QuirkId::parse("quirk.nvidia.p2p-driver-defect");
  driver_defect.quirk_generation = QuirkGeneration::first();
  driver_defect.title = "peer-to-peer transfers are reported as supported but fail under this driver range";
  driver_defect.description =
      "The device advertises peer access; the driver range 530.0-539.99 produces transfer failures.";
  driver_defect.category = QuirkCategory::DriverDefect;
  driver_defect.severity = QuirkSeverity::Unusable;
  driver_defect.mitigation = MitigationKind::UpgradeDriver;
  driver_defect.applicability.families.push_back(HardwareFamilyId::parse("family.nvidia.datacenter"));
  VersionRange driver_range;
  driver_range.minimum = SoftwareVersion::parse("530.0");
  driver_range.maximum = SoftwareVersion::parse("540.0");
  driver_defect.applicability.driver_ranges.push_back(driver_range);
  driver_defect.impacted_capabilities.push_back(cap("cap.interconnect.peer_to_peer"));
  driver_defect.source.source_class = SourceClass::SyntheticBackend;
  driver_defect.source.provenance = ProvenanceClass::Synthetic;
  driver_defect.source.publisher.id = PublisherId::parse("publisher.synthetic");
  fleet.quirks.push_back(driver_defect);

  QuirkRecord overlapping;
  overlapping.id = QuirkId::parse("quirk.nvidia.p2p-performance-caveat");
  overlapping.quirk_generation = QuirkGeneration::first();
  overlapping.title = "peer-to-peer bandwidth is degraded on this topology";
  overlapping.category = QuirkCategory::PerformanceCaveat;
  overlapping.severity = QuirkSeverity::Caveat;
  overlapping.mitigation = MitigationKind::None;
  overlapping.applicability.families.push_back(HardwareFamilyId::parse("family.nvidia.datacenter"));
  overlapping.impacted_capabilities.push_back(cap("cap.interconnect.peer_to_peer"));
  overlapping.source.source_class = SourceClass::SyntheticBackend;
  overlapping.source.provenance = ProvenanceClass::Synthetic;
  overlapping.source.publisher.id = PublisherId::parse("publisher.synthetic");
  fleet.quirks.push_back(overlapping);
  return fleet;
}

SyntheticFleet make_unsupported_scenario() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.consumer", "model.nvidia.rtx4090",
                                    "arch.nvidia.sm_89", "560.35", "95.02.18.00.01", "12.6", "rtx4090");
  add(device, "cap.partition.mechanism", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
      "synthetic.nvmlGetMigMode");
  add(device, "cap.memory.ecc", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
      "synthetic.nvmlGetEccMode");
  add(device, "cap.interconnect.nvswitch", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
      "synthetic.nvmlTopology");
  add(device, "cap.compute.fp64", SupportState::SupportedNative, boolean(true), PrecisionClass::DirectReported,
      "synthetic.runtime");
  // Evidence absence must stay distinct from positive non-support.
  add(device, "cap.net.rdma", SupportState::Unknown, CapabilityValue{}, PrecisionClass::Unknown,
      "synthetic.none");
  fleet.devices.push_back(device);
  return fleet;
}

SyntheticFleet make_reset_scenario() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.consumer", "model.nvidia.rtx5090",
                                    "arch.nvidia.sm_120", "616.92", "98.02.5c.00.0a", "12.9", "reset-gpu");
  add(device, "cap.memory.capacity", SupportState::SupportedNative, bytes_value(34251081216ull),
      PrecisionClass::DirectReported, "synthetic.nvml");
  add(device, "cap.reliability.reset", SupportState::SupportedNative,
      enum_value("reset", "device-level"), PrecisionClass::DirectReported, "synthetic.nvml");
  fleet.devices.push_back(device);

  std::vector<SyntheticCapability> after_reset;
  {
    SyntheticCapability entry;
    entry.capability = "cap.memory.capacity";
    entry.support = SupportState::SupportedNative;
    entry.value = bytes_value(34251081216ull);
    entry.precision = PrecisionClass::DirectReported;
    entry.reference = "synthetic.nvml";
    after_reset.push_back(entry);
  }
  {
    SyntheticCapability entry;
    entry.capability = "cap.runtime.ipc";
    entry.support = SupportState::SupportedNative;
    entry.value = boolean(true);
    entry.precision = PrecisionClass::DirectReported;
    entry.reference = "synthetic.runtime";
    after_reset.push_back(entry);
  }
  fleet.reincarnations.emplace_back(0u, after_reset);
  return fleet;
}

SyntheticFleet make_precision_matrix() {
  SyntheticFleet fleet;
  SyntheticDevice device = make_gpu("vendor.nvidia", "family.nvidia.datacenter", "model.nvidia.b200",
                                    "arch.nvidia.sm_100", "570.86.15", "97.00.10.00.01", "12.8", "b200");
  add_classified(device, "cap.compute.fp4", SupportState::SupportedNative, boolean(true),
                 PrecisionClass::Curated, "curated.architecture-table", SourceClass::StaticCuratedDatabase,
                 ProvenanceClass::CuratedStatic);
  add_classified(device, "cap.compute.fp8", SupportState::SupportedNative, boolean(true), PrecisionClass::Probed,
                 "synthetic.runtime-probe", SourceClass::HardwareProbe, ProvenanceClass::RealLiveHardware);
  add_classified(device, "cap.compute.bf16", SupportState::SupportedNative, boolean(true),
                 PrecisionClass::DirectReported, "synthetic.nvml", SourceClass::LiveVendorApi,
                 ProvenanceClass::RealVendorApi);
  add_classified(device, "cap.compute.fp64", SupportState::SupportedNative, boolean(true),
                 PrecisionClass::Inferred, "derived.from-family", SourceClass::StaticCuratedDatabase,
                 ProvenanceClass::Derived);
  fleet.devices.push_back(device);
  return fleet;
}

SyntheticFleet build_fleet(SyntheticProfile profile) {
  switch (profile) {
    case SyntheticProfile::MixedVendorFleet: return make_mixed_vendor_fleet();
    case SyntheticProfile::NvidiaGenerationDrift: return make_generation_drift();
    case SyntheticProfile::AmdAcceleratorFleet: return make_amd_fleet();
    case SyntheticProfile::CxlFabric: return make_cxl_fabric();
    case SyntheticProfile::DpuSmartNicFabric: return make_dpu_fabric();
    case SyntheticProfile::PartitioningProfiles: return make_partitioning();
    case SyntheticProfile::ContradictionScenario: return make_contradiction();
    case SyntheticProfile::QuirkScenario: return make_quirk_scenario();
    case SyntheticProfile::UnsupportedScenario: return make_unsupported_scenario();
    case SyntheticProfile::ResetScenario: return make_reset_scenario();
    case SyntheticProfile::PrecisionMatrix: return make_precision_matrix();
  }
  return make_mixed_vendor_fleet();
}

DeviceRegistration registration_for(const SyntheticDevice& device, const DiscoveryContext& context) {
  DeviceRegistration registration;
  DeviceIdentity identity;
  identity.id = canonical_device_id("dev.synthetic", device.token);
  identity.incarnation = DeviceIncarnationId::parse(
      "inc.synthetic." + canonical_token(device.token) + "." + canonical_token(device.model));
  identity.hardware.vendor = VendorId::parse(device.vendor);
  identity.hardware.product = ProductId::parse("product." + canonical_token(device.model));
  identity.hardware.family = HardwareFamilyId::parse(device.family);
  identity.hardware.model = HardwareModelId::parse(device.model);
  identity.hardware.revision = HardwareRevisionId::parse(device.revision);
  identity.hardware.architecture = ArchitectureId::parse(device.architecture);
  identity.hardware.hardware_class = device.hardware_class;
  identity.hardware.native_identifiers = {
      {"synthetic.profile.token", device.token},
      {"synthetic.profile.model", device.model},
  };
  identity.platform = context.platform.id;
  identity.display_name = device.display;
  identity.instance_locator = device.token;
  registration.identity = identity;
  registration.software.driver.id = DriverId::parse(device.driver_id);
  registration.software.driver.version = device.driver_version;
  registration.software.firmware.id = FirmwareId::parse(device.firmware_id);
  registration.software.firmware.version = device.firmware_version;
  registration.software.runtime.id = RuntimeId::parse(device.runtime_id);
  registration.software.runtime.version = device.runtime_version;
  registration.platform = context.platform;
  registration.observed_at_utc = context.observed_at_utc;
  registration.observation_sequence = 0;
  return registration;
}

class SyntheticBackend final : public IDiscoveryBackend {
 public:
  explicit SyntheticBackend(SyntheticProfile profile) : profile_(profile) {}

  std::string adapter_name() const override { return "synthetic.backend"; }
  SourceClass source_class() const override { return SourceClass::SyntheticBackend; }
  ProvenanceClass provenance() const override { return ProvenanceClass::Synthetic; }

  bool available(std::string& reason) const override {
    reason.clear();
    return true;
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
    SyntheticFleet fleet = build_fleet(profile_);

    std::vector<SubjectContext> subjects;
    subjects.reserve(fleet.devices.size());
    for (const SyntheticDevice& device : fleet.devices) {
      const DeviceRegistration registration = registration_for(device, context);
      SubjectContext subject;
      const Status status = builder.register_subject(registration, subject);
      if (!status.ok()) {
        report = builder.report();
        report.notes.push_back("synthetic device registration failed: " + status.describe());
        return status;
      }
      subjects.push_back(subject);
      for (const SyntheticCapability& capability : device.capabilities) {
        const Status published = builder.publish_capability(
            subject, cap(capability.capability), capability.support, capability.value, capability.precision,
            capability.reference, capability.conditions, capability.source_class, capability.provenance);
        (void)published;
      }
    }

    for (const auto& reincarnation : fleet.reincarnations) {
      const std::size_t index = reincarnation.first;
      if (index >= fleet.devices.size()) continue;
      builder.bump_evidence_generation();
      DeviceRegistration registration = registration_for(fleet.devices[index], context);
      registration.device_boot_observed = true;
      registration.software.firmware.version = fleet.devices[index].firmware_version + ".post-reset";
      SubjectContext subject;
      const Status status = builder.announce_device_generation(registration, subject);
      if (!status.ok()) {
        report = builder.report();
        report.notes.push_back("synthetic reincarnation failed: " + status.describe());
        return status;
      }
      subjects[index] = subject;
      for (const SyntheticCapability& capability : reincarnation.second) {
        const Status published = builder.publish_capability(
            subject, cap(capability.capability), capability.support, capability.value, capability.precision,
            capability.reference, capability.conditions, capability.source_class, capability.provenance);
        (void)published;
      }
    }

    for (const QuirkRecord& quirk : fleet.quirks) {
      const Status status = builder.publish_quirk(quirk);
      if (!status.ok()) {
        report = builder.report();
        report.notes.push_back("synthetic quirk rejected: " + status.describe());
      }
    }
    for (const CompatibilityFact& fact : fleet.compatibility) {
      // Resolve synthetic device references to the identities that were
      // actually registered, so compatibility facts never dangle.
      CompatibilityFact record = fact;
      if (record.lhs.kind == HardwareReference::Kind::Device) {
        for (const SyntheticDevice& device : fleet.devices) {
          if (device.token == record.lhs.token) {
            record.lhs.token = canonical_device_id("dev.synthetic", device.token).str();
          }
        }
      }
      const Status status = builder.publish_compatibility(record);
      if (!status.ok()) {
        report = builder.report();
        report.notes.push_back("synthetic compatibility fact rejected: " + status.describe());
      }
    }
    report = builder.report();
    report.notes.push_back(std::string("synthetic profile ") + to_string(profile_) + " published " +
                           std::to_string(subjects.size()) + " devices");
    return Status::success();
  }

 private:
  SyntheticProfile profile_;
};

}  // namespace

const char* to_string(SyntheticProfile profile) noexcept {
  switch (profile) {
    case SyntheticProfile::MixedVendorFleet: return "mixed-vendor-fleet";
    case SyntheticProfile::NvidiaGenerationDrift: return "nvidia-generation-drift";
    case SyntheticProfile::AmdAcceleratorFleet: return "amd-accelerator-fleet";
    case SyntheticProfile::CxlFabric: return "cxl-fabric";
    case SyntheticProfile::DpuSmartNicFabric: return "dpu-smartnic-fabric";
    case SyntheticProfile::PartitioningProfiles: return "partitioning-profiles";
    case SyntheticProfile::ContradictionScenario: return "contradiction-scenario";
    case SyntheticProfile::QuirkScenario: return "quirk-scenario";
    case SyntheticProfile::UnsupportedScenario: return "unsupported-scenario";
    case SyntheticProfile::ResetScenario: return "reset-scenario";
    case SyntheticProfile::PrecisionMatrix: return "precision-matrix";
  }
  return "mixed-vendor-fleet";
}

bool synthetic_profile_from_string(std::string_view text, SyntheticProfile& out) noexcept {
  for (int i = 0; i <= static_cast<int>(SyntheticProfile::PrecisionMatrix); ++i) {
    const auto profile = static_cast<SyntheticProfile>(i);
    if (equals_ascii_ci(text, to_string(profile))) {
      out = profile;
      return true;
    }
  }
  return false;
}

std::vector<SyntheticProfile> all_synthetic_profiles() {
  std::vector<SyntheticProfile> profiles;
  for (int i = 0; i <= static_cast<int>(SyntheticProfile::PrecisionMatrix); ++i) {
    profiles.push_back(static_cast<SyntheticProfile>(i));
  }
  return profiles;
}

namespace adapters {

std::unique_ptr<IDiscoveryBackend> make_synthetic_backend(SyntheticProfile profile) {
  return std::make_unique<SyntheticBackend>(profile);
}

}  // namespace adapters

}  // namespace hcr
