// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "hcr/condition.hpp"
#include "hcr/discovery.hpp"

namespace hcr {

/// Device identity derived from a vendor GPU UUID. NVML and the CUDA driver
/// API report the same UUID, so both adapters resolve to exactly one subject
/// and their evidence corroborates rather than duplicating devices.
DeviceId gpu_device_id_from_uuid(std::string_view uuid);

/// Deterministic synthetic fleet profiles. Synthetic evidence never outranks
/// real evidence and is always classified SYNTHETIC.
enum class SyntheticProfile {
  MixedVendorFleet = 0,
  NvidiaGenerationDrift,
  AmdAcceleratorFleet,
  CxlFabric,
  DpuSmartNicFabric,
  PartitioningProfiles,
  ContradictionScenario,
  QuirkScenario,
  UnsupportedScenario,
  ResetScenario,
  PrecisionMatrix,
};

const char* to_string(SyntheticProfile profile) noexcept;
bool synthetic_profile_from_string(std::string_view text, SyntheticProfile& out) noexcept;
std::vector<SyntheticProfile> all_synthetic_profiles();

namespace adapters {

/// Probes the host platform identity, firmware/runtime software state and the
/// environment facts that qualify host-scoped and conditional capability.
/// Read-only and vendor-neutral; adapters and tools use it to build a
/// discovery context.
Status probe_host_platform(PlatformIdentity& platform,
                           EnvironmentContext& environment,
                           DeviceSoftwareState& software,
                           std::string& detail);

/// One enumerated PCI function. Only fields the operating system actually
/// reports are populated; an empty field means the value is unavailable, never
/// that it is false.
struct PciDeviceRecord {
  std::string instance_id;
  std::string vendor_id;
  std::string device_id;
  std::string subsystem_id;
  std::string revision;
  std::string class_code;  ///< six hex digits from the compatible-id class code
  std::string description;
};

/// Enumerates present PCI functions through the platform device enumerator.
Status enumerate_pci_devices(std::vector<PciDeviceRecord>& out, std::string& detail);

/// Real hardware adapters. Each returns a backend that reports honestly
/// whether it can run; an unavailable backend publishes nothing rather than
/// guessing.
std::unique_ptr<IDiscoveryBackend> make_cpu_platform_backend();
std::unique_ptr<IDiscoveryBackend> make_pci_topology_backend();
std::unique_ptr<IDiscoveryBackend> make_nvml_backend();
std::unique_ptr<IDiscoveryBackend> make_cuda_runtime_backend();
std::unique_ptr<IDiscoveryBackend> make_network_backend();
std::unique_ptr<IDiscoveryBackend> make_cxl_backend();

/// Deterministic synthetic backend used to exercise the same production
/// pipeline with hardware that is not physically present.
std::unique_ptr<IDiscoveryBackend> make_synthetic_backend(SyntheticProfile profile);

}  // namespace adapters
}  // namespace hcr
