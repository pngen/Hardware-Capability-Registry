// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hcr/ids.hpp"
#include "hcr/limits.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Hardware-class taxonomy for the accelerator infrastructure stack.
///
/// Classes share a common identity core (vendor, family, model, revision,
/// architecture, platform) but keep class-specific capability namespaces, so
/// incompatible device classes are never forced into one flat schema.
enum class HardwareClass {
  Unknown = 0,
  Cpu,
  Gpu,
  Npu,
  TpuAccelerator,
  Fpga,
  AsicAccelerator,
  Nic,
  SmartNic,
  Dpu,
  CxlDevice,
  MemoryExpander,
  HbmDomain,
  DramDomain,
  PersistentMemory,
  PcieSwitch,
  NvSwitchLike,
  FabricSwitch,
  StorageController,
  NvmeDevice,
  /// A network adapter or port entity subordinate to a NIC device.
  NetworkAdapter,
  OffloadEngine,
  Other,
};

const char* to_string(HardwareClass value) noexcept;
bool hardware_class_from_string(std::string_view text, HardwareClass& out) noexcept;

/// Backend-native identifier retained verbatim as provenance metadata. Native
/// strings never participate in canonical identity.
struct NativeIdentifier {
  std::string key;
  std::string value;
  friend bool operator==(const NativeIdentifier& a, const NativeIdentifier& b) noexcept {
    return a.key == b.key && a.value == b.value;
  }
  friend bool operator<(const NativeIdentifier& a, const NativeIdentifier& b) noexcept {
    if (a.key != b.key) return a.key < b.key;
    return a.value < b.value;
  }
};

/// Multi-level hardware identity. Revision and architecture may be unavailable;
/// an unavailable level is represented by an invalid Id, never by a placeholder
/// token that could be mistaken for a real identity.
struct HardwareIdentity {
  VendorId vendor;
  ProductId product;
  HardwareFamilyId family;
  HardwareModelId model;
  HardwareRevisionId revision;
  ArchitectureId architecture;
  HardwareClass hardware_class = HardwareClass::Unknown;
  std::vector<NativeIdentifier> native_identifiers;

  /// Canonical identity key: the levels that must exist for a subject to be
  /// referenced as authoritative capability knowledge.
  [[nodiscard]] std::string canonical_key() const;
};

Status validate_hardware_identity(const HardwareIdentity& identity, const RegistryLimits& limits);
void normalize_hardware_identity(HardwareIdentity& identity);

/// A physical hardware subject.
struct DeviceIdentity {
  DeviceId id;
  /// Driver-visible incarnation (for example a vendor UUID or a stable PCI
  /// path). A changed incarnation under the same DeviceId is a device
  /// reincarnation and opens a new capability generation boundary.
  DeviceIncarnationId incarnation;
  HardwareIdentity hardware;
  PlatformId platform;
  /// Serial numbers are frequently absent or virtualized; never required.
  std::optional<std::string> serial_number;
  /// Bus address and similar locators are explicitly instance-local and are
  /// never used as globally stable identity.
  std::optional<std::string> instance_locator;
  std::string display_name;
};

Status validate_device_identity(const DeviceIdentity& identity, const RegistryLimits& limits);

/// Host/platform identity. Platform generation advances when the platform
/// descriptor changes (for example a BIOS/UEFI upgrade), which is a real
/// staleness boundary for platform-qualified capability.
struct PlatformIdentity {
  PlatformId id;
  std::string display_name;
  std::string vendor;
  std::string model;
  ArchitectureId architecture;
  std::vector<NativeIdentifier> native_identifiers;
};

struct FirmwareIdentity {
  FirmwareId id;
  std::string version;
  std::string build;
  friend bool operator==(const FirmwareIdentity& a, const FirmwareIdentity& b) noexcept {
    return a.id == b.id && a.version == b.version && a.build == b.build;
  }
};

struct DriverIdentity {
  DriverId id;
  std::string version;
  friend bool operator==(const DriverIdentity& a, const DriverIdentity& b) noexcept {
    return a.id == b.id && a.version == b.version;
  }
};

struct RuntimeIdentity {
  RuntimeId id;
  std::string version;
  friend bool operator==(const RuntimeIdentity& a, const RuntimeIdentity& b) noexcept {
    return a.id == b.id && a.version == b.version;
  }
};

/// Software state that qualifies capability evidence for one device.
struct DeviceSoftwareState {
  FirmwareIdentity firmware;
  FirmwareGeneration firmware_generation;
  DriverIdentity driver;
  DriverGeneration driver_generation;
  RuntimeIdentity runtime;
  RuntimeGeneration runtime_generation;

  friend bool operator==(const DeviceSoftwareState& a, const DeviceSoftwareState& b) noexcept {
    return a.firmware == b.firmware && a.driver == b.driver && a.runtime == b.runtime;
  }
};

/// Registration payload for a physical device. The registry owns generation
/// assignment; callers never choose generations.
struct DeviceRegistration {
  DeviceIdentity identity;
  DeviceSoftwareState software;
  PlatformIdentity platform;
  /// When true the caller asserts it observed a device boot/reset that the
  /// registry has not yet observed. The registry advances the boot counter and
  /// opens a new capability generation boundary.
  bool device_boot_observed = false;
  /// Monotonic counter supplied by the discovering publisher; used only to
  /// order observations from one publisher, never as authority.
  std::uint64_t observation_sequence = 0;
  std::string observed_at_utc;
};

}  // namespace hcr
