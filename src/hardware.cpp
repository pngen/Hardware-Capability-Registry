// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/hardware.hpp"

#include <algorithm>

#include "hcr/canonical.hpp"

namespace hcr {

const char* to_string(HardwareClass value) noexcept {
  switch (value) {
    case HardwareClass::Unknown: return "unknown";
    case HardwareClass::Cpu: return "cpu";
    case HardwareClass::Gpu: return "gpu";
    case HardwareClass::Npu: return "npu";
    case HardwareClass::TpuAccelerator: return "tpu-accelerator";
    case HardwareClass::Fpga: return "fpga";
    case HardwareClass::AsicAccelerator: return "asic-accelerator";
    case HardwareClass::Nic: return "nic";
    case HardwareClass::SmartNic: return "smartnic";
    case HardwareClass::Dpu: return "dpu";
    case HardwareClass::CxlDevice: return "cxl-device";
    case HardwareClass::MemoryExpander: return "memory-expander";
    case HardwareClass::HbmDomain: return "hbm-domain";
    case HardwareClass::DramDomain: return "dram-domain";
    case HardwareClass::PersistentMemory: return "persistent-memory";
    case HardwareClass::PcieSwitch: return "pcie-switch";
    case HardwareClass::NvSwitchLike: return "nvswitch-like";
    case HardwareClass::FabricSwitch: return "fabric-switch";
    case HardwareClass::StorageController: return "storage-controller";
    case HardwareClass::NvmeDevice: return "nvme-device";
    case HardwareClass::NetworkAdapter: return "network-adapter";
    case HardwareClass::OffloadEngine: return "offload-engine";
    case HardwareClass::Other: return "other";
  }
  return "unknown";
}

bool hardware_class_from_string(std::string_view text, HardwareClass& out) noexcept {
  for (int i = 0; i <= static_cast<int>(HardwareClass::Other); ++i) {
    const auto value = static_cast<HardwareClass>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

std::string HardwareIdentity::canonical_key() const {
  std::string key;
  key += vendor.valid() ? vendor.str() : std::string("-");
  key += "|";
  key += product.valid() ? product.str() : std::string("-");
  key += "|";
  key += family.valid() ? family.str() : std::string("-");
  key += "|";
  key += model.valid() ? model.str() : std::string("-");
  key += "|";
  key += revision.valid() ? revision.str() : std::string("-");
  key += "|";
  key += architecture.valid() ? architecture.str() : std::string("-");
  return key;
}

void normalize_hardware_identity(HardwareIdentity& identity) {
  std::sort(identity.native_identifiers.begin(), identity.native_identifiers.end());
  identity.native_identifiers.erase(
      std::unique(identity.native_identifiers.begin(), identity.native_identifiers.end()),
      identity.native_identifiers.end());
}

Status validate_hardware_identity(const HardwareIdentity& identity, const RegistryLimits& limits) {
  if (identity.native_identifiers.size() > limits.max_native_identifiers) {
    return Status::failure(ErrorCode::LimitExceeded, "too many native identifiers");
  }
  for (const NativeIdentifier& identifier : identity.native_identifiers) {
    if (identifier.key.empty()) {
      return Status::failure(ErrorCode::InvalidIdentity, "native identifier without key");
    }
    if (identifier.value.size() > limits.max_metadata_bytes) {
      return Status::failure(ErrorCode::LimitExceeded, "native identifier value exceeds length limit");
    }
  }
  if (!identity.vendor.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "hardware identity requires a vendor");
  }
  if (!identity.family.valid() && !identity.model.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "hardware identity requires a family or a model");
  }
  if (identity.hardware_class == HardwareClass::Unknown) {
    return Status::failure(ErrorCode::InvalidIdentity, "hardware identity requires a hardware class");
  }
  return Status::success();
}

Status validate_device_identity(const DeviceIdentity& identity, const RegistryLimits& limits) {
  if (!identity.id.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "device identity requires a canonical device id");
  }
  if (!identity.incarnation.valid()) {
    return Status::failure(ErrorCode::InvalidIdentity, "device identity requires an incarnation id");
  }
  if (identity.display_name.size() > limits.max_name_length) {
    return Status::failure(ErrorCode::LimitExceeded, "device display name exceeds length limit");
  }
  if (identity.serial_number.has_value() && identity.serial_number->size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "serial number exceeds length limit");
  }
  if (identity.instance_locator.has_value() && identity.instance_locator->size() > limits.max_metadata_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "instance locator exceeds length limit");
  }
  return validate_hardware_identity(identity.hardware, limits);
}

}  // namespace hcr
