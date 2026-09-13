// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/catalog.hpp"

namespace hcr {
namespace {

CapabilitySchema make(const char* schema_id, CapabilityDomain domain, ValueKind kind, const char* name,
                      const char* unit = "") {
  CapabilitySchema schema;
  schema.id = CapabilitySchemaId::parse(schema_id);
  const std::string token(schema_id);
  const std::size_t marker = token.rfind(".v");
  schema.capability = CapabilityId::parse(marker == std::string::npos ? token : token.substr(0, marker));
  schema.domain = domain;
  schema.value_kind = kind;
  schema.canonical_name = name;
  schema.unit = unit;
  return schema;
}

std::vector<CapabilitySchema> build_catalog() {
  std::vector<CapabilitySchema> catalog;
  const auto add = [&catalog](CapabilitySchema schema) { catalog.push_back(std::move(schema)); };

  // -- compute ------------------------------------------------------------
  add(make("cap.compute.architecture.v1", CapabilityDomain::Compute, ValueKind::ArchitectureSet,
           "Execution architectures exposed by the device"));
  add(make("cap.compute.capability.v1", CapabilityDomain::Compute, ValueKind::Enumeration,
           "Vendor compute capability level", "domain=compute_capability"));
  add(make("cap.compute.execution_units.v1", CapabilityDomain::Compute, ValueKind::Count,
           "Parallel execution units", "units"));
  add(make("cap.compute.tensor_units.v1", CapabilityDomain::Compute, ValueKind::Count,
           "Matrix/tensor execution units", "units"));
  add(make("cap.compute.multiprocessors.v1", CapabilityDomain::Compute, ValueKind::Count,
           "Streaming multiprocessors, as reported by the vendor driver API", "multiprocessors"));
  add(make("cap.compute.vector_width_bits.v1", CapabilityDomain::Compute, ValueKind::Count,
           "SIMD/vector width in bits", "bits"));
  add(make("cap.compute.datatypes.v1", CapabilityDomain::Compute, ValueKind::FeatureSet,
           "Data types the device can execute natively"));
  add(make("cap.compute.fp64.v1", CapabilityDomain::Compute, ValueKind::Boolean, "FP64 execution"));
  add(make("cap.compute.fp32.v1", CapabilityDomain::Compute, ValueKind::Boolean, "FP32 execution"));
  add(make("cap.compute.tf32.v1", CapabilityDomain::Compute, ValueKind::Boolean, "TF32 execution"));
  add(make("cap.compute.fp16.v1", CapabilityDomain::Compute, ValueKind::Boolean, "FP16 execution"));
  add(make("cap.compute.bf16.v1", CapabilityDomain::Compute, ValueKind::Boolean, "BF16 execution"));
  add(make("cap.compute.fp8.v1", CapabilityDomain::Compute, ValueKind::Boolean, "FP8 execution"));
  add(make("cap.compute.fp4.v1", CapabilityDomain::Compute, ValueKind::Boolean, "FP4 execution"));
  add(make("cap.compute.int8.v1", CapabilityDomain::Compute, ValueKind::Boolean, "INT8 execution"));
  add(make("cap.compute.int4.v1", CapabilityDomain::Compute, ValueKind::Boolean, "INT4 execution"));
  add(make("cap.compute.sparse_execution.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Structured sparse execution"));
  add(make("cap.compute.atomics.v1", CapabilityDomain::Compute, ValueKind::FeatureSet,
           "Atomic operations exposed to device code"));
  add(make("cap.compute.cooperative_execution.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Cooperative grid/block execution"));
  add(make("cap.compute.dynamic_parallelism.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Device-side launch of additional work"));
  add(make("cap.compute.concurrent_kernels.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Multiple kernels executing concurrently on one device"));
  add(make("cap.compute.preemption.v1", CapabilityDomain::Compute, ValueKind::Enumeration,
           "Preemption granularity", "domain=preemption"));
  add(make("cap.compute.context_isolation.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Hardware context isolation between clients"));
  add(make("cap.compute.programmable.v1", CapabilityDomain::Compute, ValueKind::Boolean,
           "Device executes publisher-supplied programs"));

  // -- memory -------------------------------------------------------------
  add(make("cap.memory.capacity.v1", CapabilityDomain::Memory, ValueKind::Bytes, "Device memory capacity"));
  add(make("cap.memory.type.v1", CapabilityDomain::Memory, ValueKind::Enumeration, "Memory technology",
           "domain=memory_type"));
  add(make("cap.memory.bandwidth.v1", CapabilityDomain::Memory, ValueKind::Bandwidth,
           "Nominal memory bandwidth"));
  add(make("cap.memory.bus_width_bits.v1", CapabilityDomain::Memory, ValueKind::Count,
           "Memory bus width in bits", "bits"));
  add(make("cap.memory.ecc.v1", CapabilityDomain::Memory, ValueKind::Enumeration, "ECC state",
           "domain=ecc"));
  add(make("cap.memory.ecc_error_counters.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "ECC error counters are readable"));
  add(make("cap.memory.page_sizes.v1", CapabilityDomain::Memory, ValueKind::FeatureSet,
           "Supported page sizes"));
  add(make("cap.memory.unified_addressing.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "Unified virtual addressing across host and device"));
  add(make("cap.memory.peer_access.v1", CapabilityDomain::Memory, ValueKind::FeatureSet,
           "Peer-visible memory access paths"));
  add(make("cap.memory.managed_memory.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "Driver-managed unified memory"));
  add(make("cap.memory.coherence.v1", CapabilityDomain::Memory, ValueKind::Enumeration,
           "Coherence model", "domain=coherence"));
  add(make("cap.memory.address_translation.v1", CapabilityDomain::Memory, ValueKind::FeatureSet,
           "Address translation features"));
  add(make("cap.memory.pinned_host_interaction.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "Pinned host memory interaction"));
  add(make("cap.memory.partition_local.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "Partition-local memory semantics"));
  add(make("cap.memory.cxl_exposure.v1", CapabilityDomain::Memory, ValueKind::Boolean,
           "CXL-class memory exposure"));
  add(make("cap.memory.numa_nodes.v1", CapabilityDomain::Memory, ValueKind::Count, "NUMA nodes", "nodes"));

  // -- partitioning -------------------------------------------------------
  add(make("cap.partition.mechanism.v1", CapabilityDomain::Partitioning, ValueKind::FeatureSet,
           "Physical partitioning mechanisms"));
  add(make("cap.partition.profiles.v1", CapabilityDomain::Partitioning, ValueKind::FeatureSet,
           "Supported partition profiles"));
  add(make("cap.partition.max_partitions.v1", CapabilityDomain::Partitioning, ValueKind::Count,
           "Maximum concurrent partitions", "partitions"));
  add(make("cap.partition.memory_isolation.v1", CapabilityDomain::Partitioning, ValueKind::Boolean,
           "Memory isolation between partitions"));
  add(make("cap.partition.compute_isolation.v1", CapabilityDomain::Partitioning, ValueKind::Boolean,
           "Compute isolation between partitions"));
  add(make("cap.partition.live_reconfiguration.v1", CapabilityDomain::Partitioning, ValueKind::Enumeration,
           "Reconfiguration without reset", "domain=live_reconfiguration"));
  add(make("cap.partition.reset_required.v1", CapabilityDomain::Partitioning, ValueKind::Boolean,
           "A device reset is required to apply a partition change"));
  add(make("cap.partition.migration.v1", CapabilityDomain::Partitioning, ValueKind::Enumeration,
           "Partition migration support", "domain=migration"));
  add(make("cap.partition.sriov.v1", CapabilityDomain::Partitioning, ValueKind::Boolean,
           "SR-IOV virtual function exposure"));
  add(make("cap.partition.mediated_devices.v1", CapabilityDomain::Partitioning, ValueKind::Boolean,
           "Mediated/virtual device support"));

  // -- interconnect -------------------------------------------------------
  add(make("cap.interconnect.pcie_generation.v1", CapabilityDomain::Interconnect, ValueKind::Count,
           "Negotiated PCIe generation", "generation"));
  add(make("cap.interconnect.pcie_width.v1", CapabilityDomain::Interconnect, ValueKind::Count,
           "Negotiated PCIe link width", "lanes"));
  add(make("cap.interconnect.peer_to_peer.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "Direct peer-to-peer transfer between devices"));
  add(make("cap.interconnect.link_bandwidth.v1", CapabilityDomain::Interconnect, ValueKind::Bandwidth,
           "Nominal interconnect bandwidth"));
  add(make("cap.interconnect.collective_offload.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "Collective communication offload"));
  add(make("cap.interconnect.nvlink.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "NVLink-class cache-coherent device interconnect"));
  add(make("cap.interconnect.nvswitch.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "NVSwitch-class switched fabric presence"));
  add(make("cap.interconnect.infinity_fabric.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "Infinity Fabric-class coherent interconnect"));
  add(make("cap.interconnect.cxl.v1", CapabilityDomain::Interconnect, ValueKind::Boolean,
           "CXL link capability"));
  add(make("cap.interconnect.topology_features.v1", CapabilityDomain::Interconnect, ValueKind::FeatureSet,
           "Link topology features"));

  // -- networking ---------------------------------------------------------
  add(make("cap.net.rdma.v1", CapabilityDomain::Networking, ValueKind::Boolean, "RDMA capability"));
  add(make("cap.net.roce.v1", CapabilityDomain::Networking, ValueKind::Boolean, "RoCE capability"));
  add(make("cap.net.infiniband.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "InfiniBand-class link"));
  add(make("cap.net.gpudirect.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "GPUDirect-class peer memory path"));
  add(make("cap.net.dma.v1", CapabilityDomain::Networking, ValueKind::Boolean, "Bus-master DMA"));
  add(make("cap.net.scatter_gather.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Scatter/gather DMA"));
  add(make("cap.net.checksum_offload.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Checksum offload"));
  add(make("cap.net.encryption_offload.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Encryption offload"));
  add(make("cap.net.compression_offload.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Compression offload"));
  add(make("cap.net.smartnic_processing.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Programmable on-adapter processing"));
  add(make("cap.net.queue_pairs.v1", CapabilityDomain::Networking, ValueKind::Count,
           "Hardware queue pairs", "queues"));
  add(make("cap.net.sriov.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "SR-IOV virtual function exposure"));
  add(make("cap.net.flow_steering.v1", CapabilityDomain::Networking, ValueKind::Boolean,
           "Hardware flow steering"));
  add(make("cap.net.link_speed.v1", CapabilityDomain::Networking, ValueKind::Bandwidth,
           "Negotiated link speed"));
  add(make("cap.net.interface_state.v1", CapabilityDomain::Networking, ValueKind::Enumeration,
           "Interface administrative/operational state", "domain=interface_state"));
  add(make("cap.net.mtu.v1", CapabilityDomain::Networking, ValueKind::Count, "Maximum transmission unit",
           "bytes"));

  // -- runtime ------------------------------------------------------------
  add(make("cap.runtime.cuda.v1", CapabilityDomain::Runtime, ValueKind::Enumeration,
           "CUDA runtime/driver availability", "domain=runtime"));
  add(make("cap.runtime.rocm.v1", CapabilityDomain::Runtime, ValueKind::Enumeration,
           "ROCm runtime availability", "domain=runtime"));
  add(make("cap.runtime.level_zero.v1", CapabilityDomain::Runtime, ValueKind::Enumeration,
           "Level Zero runtime availability", "domain=runtime"));
  add(make("cap.runtime.driver_api.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "Low-level driver API exposure"));
  add(make("cap.runtime.runtime_api.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "High-level runtime API exposure"));
  add(make("cap.runtime.compiler_targets.v1", CapabilityDomain::Runtime, ValueKind::FeatureSet,
           "Compiler targets accepted by the device"));
  add(make("cap.runtime.binary_formats.v1", CapabilityDomain::Runtime, ValueKind::FeatureSet,
           "Accepted binary container formats"));
  add(make("cap.runtime.kernel_architectures.v1", CapabilityDomain::Runtime, ValueKind::ArchitectureSet,
           "Kernel instruction architectures"));
  add(make("cap.runtime.graph_capture.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "Execution graph capture"));
  add(make("cap.runtime.ipc.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "Inter-process device memory sharing"));
  add(make("cap.runtime.external_memory_handles.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "Import/export of external memory handles"));
  add(make("cap.runtime.external_semaphores.v1", CapabilityDomain::Runtime, ValueKind::Boolean,
           "External semaphore/event interop"));

  // -- reliability --------------------------------------------------------
  add(make("cap.reliability.ecc_exposure.v1", CapabilityDomain::Reliability, ValueKind::Boolean,
           "ECC state is exposed to software"));
  add(make("cap.reliability.reset.v1", CapabilityDomain::Reliability, ValueKind::Enumeration,
           "Reset capability", "domain=reset"));
  add(make("cap.reliability.device_recovery.v1", CapabilityDomain::Reliability, ValueKind::Boolean,
           "Device recovery without host reboot"));
  add(make("cap.reliability.error_containment.v1", CapabilityDomain::Reliability, ValueKind::Boolean,
           "Fault containment between clients"));
  add(make("cap.reliability.fault_reporting.v1", CapabilityDomain::Reliability, ValueKind::FeatureSet,
           "Fault reporting channels"));
  add(make("cap.reliability.telemetry_visibility.v1", CapabilityDomain::Reliability, ValueKind::FeatureSet,
           "Telemetry surfaces visible to software"));
  add(make("cap.reliability.xid_reporting.v1", CapabilityDomain::Reliability, ValueKind::Boolean,
           "Structured error identifiers"));

  // -- power --------------------------------------------------------------
  add(make("cap.power.telemetry.v1", CapabilityDomain::Power, ValueKind::Boolean, "Power telemetry"));
  add(make("cap.power.cap_range.v1", CapabilityDomain::Power, ValueKind::NumericRange,
           "Settable power cap range in milliwatts", "mW"));
  add(make("cap.power.clock_control.v1", CapabilityDomain::Power, ValueKind::FeatureSet,
           "Clock domain control"));
  add(make("cap.power.thermal_sensors.v1", CapabilityDomain::Power, ValueKind::Count,
           "Thermal sensors", "sensors"));
  add(make("cap.power.fan_control.v1", CapabilityDomain::Power, ValueKind::Boolean, "Fan control"));
  add(make("cap.power.throttle_reasons.v1", CapabilityDomain::Power, ValueKind::FeatureSet,
           "Throttle reason reporting"));

  // -- platform -----------------------------------------------------------
  add(make("cap.platform.os.v1", CapabilityDomain::Platform, ValueKind::Enumeration,
           "Operating system family", "domain=os"));
  add(make("cap.platform.kernel.v1", CapabilityDomain::Platform, ValueKind::Enumeration,
           "Kernel version", "domain=kernel"));
  add(make("cap.platform.cpu.logical_processors.v1", CapabilityDomain::Platform, ValueKind::Count,
           "Logical processors", "processors"));
  add(make("cap.platform.cpu.physical_cores.v1", CapabilityDomain::Platform, ValueKind::Count,
           "Physical cores", "cores"));
  add(make("cap.platform.cpu.isa_features.v1", CapabilityDomain::Platform, ValueKind::FeatureSet,
           "ISA features detected on this platform"));
  add(make("cap.platform.cache_hierarchy.v1", CapabilityDomain::Platform, ValueKind::Record,
           "Cache hierarchy levels"));
  add(make("cap.platform.page_sizes.v1", CapabilityDomain::Platform, ValueKind::FeatureSet,
           "Platform page sizes"));
  add(make("cap.platform.iommu.v1", CapabilityDomain::Platform, ValueKind::Boolean,
           "IOMMU/translation hardware present and active"));
  add(make("cap.platform.pci_devices.v1", CapabilityDomain::Platform, ValueKind::Count,
           "PCI devices enumerated", "devices"));
  add(make("cap.platform.cxl_devices.v1", CapabilityDomain::Platform, ValueKind::Count,
           "CXL devices enumerated", "devices"));
  add(make("cap.platform.nvswitch_devices.v1", CapabilityDomain::Platform, ValueKind::Count,
           "NVSwitch-class devices enumerated", "devices"));
  add(make("cap.platform.accelerators.v1", CapabilityDomain::Platform, ValueKind::Count,
           "Hardware accelerators enumerated", "devices"));

  return catalog;
}

}  // namespace

const std::vector<CapabilitySchema>& builtin_capability_schemas() {
  static const std::vector<CapabilitySchema> kCatalog = build_catalog();
  return kCatalog;
}

const CapabilitySchema* find_builtin_schema_by_capability(const CapabilityId& capability) {
  for (const CapabilitySchema& schema : builtin_capability_schemas()) {
    if (schema.capability == capability) return &schema;
  }
  return nullptr;
}

const CapabilitySchema* find_builtin_schema(const CapabilitySchemaId& schema_id) {
  for (const CapabilitySchema& schema : builtin_capability_schemas()) {
    if (schema.id == schema_id) return &schema;
  }
  return nullptr;
}

}  // namespace hcr
