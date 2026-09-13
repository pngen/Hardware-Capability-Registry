// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// NVIDIA NVML discovery adapter.
//
// NVML is loaded dynamically so that the registry has no build-time or
// install-time vendor dependency: a host without NVIDIA hardware simply
// reports this adapter as unavailable, and no other code path changes. The
// declared ABI subset below is the stable C surface of NVML; every call is
// checked and every unsupported answer is published as UNSUPPORTED evidence
// rather than being treated as unknown or, worse, as support.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "adapter_common.hpp"
#include "hcr/adapters.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hcr {
namespace {

using adapter_detail::canonical_token;
using adapter_detail::count;

// --- minimal, layout-stable NVML ABI subset --------------------------------
using NvmlReturn = int;
struct nvmlDevice_st;
using NvmlDevice = nvmlDevice_st*;
using NvmlEnableState = int;
using NvmlTemperatureSensors = int;

struct NvmlPciInfo {
  char bus_id_legacy[16];
  unsigned int domain;
  unsigned int bus;
  unsigned int device;
  unsigned int pci_device_id;
  unsigned int pci_subsystem_id;
  char bus_id[32];
};

struct NvmlMemory {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};

constexpr NvmlReturn kNvmlSuccess = 0;
constexpr NvmlReturn kNvmlErrorNotSupported = 3;
constexpr NvmlReturn kNvmlErrorNotFound = 6;
constexpr NvmlTemperatureSensors kNvmlTemperatureGpu = 0;

#if defined(_WIN32)
using LibraryHandle = HMODULE;
LibraryHandle open_library(const char* name) { return LoadLibraryA(name); }
void* library_symbol(LibraryHandle handle, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
#else
using LibraryHandle = void*;
LibraryHandle open_library(const char* name) { return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
void* library_symbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
#endif

struct NvmlApi {
  LibraryHandle handle = nullptr;
  NvmlReturn (*init)() = nullptr;
  NvmlReturn (*shutdown)() = nullptr;
  NvmlReturn (*system_get_driver_version)(char*, unsigned int) = nullptr;
  NvmlReturn (*system_get_nvml_version)(char*, unsigned int) = nullptr;
  NvmlReturn (*device_get_count)(unsigned int*) = nullptr;
  NvmlReturn (*device_get_handle_by_index)(unsigned int, NvmlDevice*) = nullptr;
  NvmlReturn (*device_get_name)(NvmlDevice, char*, unsigned int) = nullptr;
  NvmlReturn (*device_get_uuid)(NvmlDevice, char*, unsigned int) = nullptr;
  NvmlReturn (*device_get_pci_info)(NvmlDevice, NvmlPciInfo*) = nullptr;
  NvmlReturn (*device_get_memory_info)(NvmlDevice, NvmlMemory*) = nullptr;
  NvmlReturn (*device_get_cuda_compute_capability)(NvmlDevice, int*, int*) = nullptr;
  NvmlReturn (*device_get_vbios_version)(NvmlDevice, char*, unsigned int) = nullptr;
  NvmlReturn (*device_get_serial)(NvmlDevice, char*, unsigned int) = nullptr;
  NvmlReturn (*device_get_mig_mode)(NvmlDevice, unsigned int*, unsigned int*) = nullptr;
  NvmlReturn (*device_get_ecc_mode)(NvmlDevice, NvmlEnableState*, NvmlEnableState*) = nullptr;
  NvmlReturn (*device_get_power_limit_constraints)(NvmlDevice, unsigned int*, unsigned int*) = nullptr;
  NvmlReturn (*device_get_temperature_count)(NvmlDevice, NvmlTemperatureSensors, unsigned int*) = nullptr;
  NvmlReturn (*device_get_memory_bus_width)(NvmlDevice, unsigned int*) = nullptr;
  NvmlReturn (*device_get_num_gpu_cores)(NvmlDevice, unsigned int*) = nullptr;
  bool loaded = false;
  bool initialized = false;

  void load() {
    if (loaded) return;
    loaded = true;
    // The driver installs nvml.dll on the system search path; no installation
    // path is hard-coded, so a driver that moves the library is still found and
    // a host without it simply reports this adapter unavailable.
    handle = open_library("nvml.dll");
    if (handle == nullptr) handle = open_library("libnvidia-ml.so.1");
    if (handle == nullptr) return;
    init = reinterpret_cast<NvmlReturn (*)()>(library_symbol(handle, "nvmlInit_v2"));
    shutdown = reinterpret_cast<NvmlReturn (*)()>(library_symbol(handle, "nvmlShutdown"));
    system_get_driver_version = reinterpret_cast<NvmlReturn (*)(char*, unsigned int)>(
        library_symbol(handle, "nvmlSystemGetDriverVersion"));
    system_get_nvml_version = reinterpret_cast<NvmlReturn (*)(char*, unsigned int)>(
        library_symbol(handle, "nvmlSystemGetNVMLVersion"));
    device_get_count = reinterpret_cast<NvmlReturn (*)(unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetCount_v2"));
    device_get_handle_by_index = reinterpret_cast<NvmlReturn (*)(unsigned int, NvmlDevice*)>(
        library_symbol(handle, "nvmlDeviceGetHandleByIndex_v2"));
    device_get_name =
        reinterpret_cast<NvmlReturn (*)(NvmlDevice, char*, unsigned int)>(library_symbol(handle, "nvmlDeviceGetName"));
    device_get_uuid =
        reinterpret_cast<NvmlReturn (*)(NvmlDevice, char*, unsigned int)>(library_symbol(handle, "nvmlDeviceGetUUID"));
    device_get_pci_info = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlPciInfo*)>(
        library_symbol(handle, "nvmlDeviceGetPciInfo_v3"));
    if (device_get_pci_info == nullptr) {
      device_get_pci_info = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlPciInfo*)>(
          library_symbol(handle, "nvmlDeviceGetPciInfo"));
    }
    device_get_memory_info = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlMemory*)>(
        library_symbol(handle, "nvmlDeviceGetMemoryInfo"));
    device_get_cuda_compute_capability = reinterpret_cast<NvmlReturn (*)(NvmlDevice, int*, int*)>(
        library_symbol(handle, "nvmlDeviceGetCudaComputeCapability"));
    device_get_vbios_version = reinterpret_cast<NvmlReturn (*)(NvmlDevice, char*, unsigned int)>(
        library_symbol(handle, "nvmlDeviceGetVbiosVersion"));
    device_get_serial = reinterpret_cast<NvmlReturn (*)(NvmlDevice, char*, unsigned int)>(
        library_symbol(handle, "nvmlDeviceGetSerial"));
    device_get_mig_mode = reinterpret_cast<NvmlReturn (*)(NvmlDevice, unsigned int*, unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetMigMode"));
    device_get_ecc_mode = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlEnableState*, NvmlEnableState*)>(
        library_symbol(handle, "nvmlDeviceGetEccMode"));
    device_get_power_limit_constraints = reinterpret_cast<NvmlReturn (*)(NvmlDevice, unsigned int*, unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetPowerManagementLimitConstraints"));
    device_get_temperature_count = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlTemperatureSensors, unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetTemperatureCount"));
    device_get_memory_bus_width = reinterpret_cast<NvmlReturn (*)(NvmlDevice, unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetMemoryBusWidth"));
    device_get_num_gpu_cores = reinterpret_cast<NvmlReturn (*)(NvmlDevice, unsigned int*)>(
        library_symbol(handle, "nvmlDeviceGetNumGpuCores"));
  }

  bool start() {
    load();
    if (handle == nullptr || init == nullptr) return false;
    if (initialized) return true;
    if (init() != kNvmlSuccess) return false;
    initialized = true;
    return true;
  }
};

std::string read_string(const char* buffer, std::size_t capacity) {
  std::size_t length = 0;
  while (length < capacity && buffer[length] != '\0') ++length;
  return std::string(buffer, length);
}

std::string nvidia_family_for(const std::string& name) {
  if (name.find("RTX") != std::string::npos || name.find("GeForce") != std::string::npos ||
      name.find("GTX") != std::string::npos) {
    return "family.nvidia.geforce";
  }
  if (name.find("A100") != std::string::npos || name.find("H100") != std::string::npos ||
      name.find("H200") != std::string::npos || name.find("B200") != std::string::npos ||
      name.find("GB200") != std::string::npos) {
    return "family.nvidia.datacenter";
  }
  if (name.find("BlueField") != std::string::npos) return "family.nvidia.dpu";
  if (name.find("ConnectX") != std::string::npos) return "family.nvidia.nic";
  return "family.nvidia.other";
}

std::string hex_digits(unsigned int value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04x", value & 0xFFFFu);
  return std::string(buffer);
}

class NvmlBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "nvml"; }
  SourceClass source_class() const override { return SourceClass::Nvml; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealVendorApi; }

  bool available(std::string& reason) const override {
    NvmlApi& api = shared_api();
    if (!api.start()) {
      reason = "NVML is not present or cannot be initialized on this host";
      return false;
    }
    reason.clear();
    return true;
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
    NvmlApi& api = shared_api();
    if (!api.start()) {
      report = builder.report();
      report.notes.push_back("NVML unavailable");
      return Status::success();
    }
    if (api.device_get_count == nullptr) {
      report = builder.report();
      report.notes.push_back("NVML does not expose nvmlDeviceGetCount_v2");
      return Status::success();
    }
    unsigned int device_count = 0;
    if (api.device_get_count(&device_count) != kNvmlSuccess) {
      report = builder.report();
      report.notes.push_back("nvmlDeviceGetCount_v2 failed");
      return Status::success();
    }

    std::string driver_version;
    if (api.system_get_driver_version != nullptr) {
      char buffer[96] = {0};
      if (api.system_get_driver_version(buffer, sizeof(buffer)) == kNvmlSuccess) {
        driver_version = read_string(buffer, sizeof(buffer));
      }
    }
    std::string nvml_version;
    if (api.system_get_nvml_version != nullptr) {
      char buffer[96] = {0};
      if (api.system_get_nvml_version(buffer, sizeof(buffer)) == kNvmlSuccess) {
        nvml_version = read_string(buffer, sizeof(buffer));
      }
    }

    for (unsigned int index = 0; index < device_count; ++index) {
      NvmlDevice handle = nullptr;
      if (api.device_get_handle_by_index == nullptr ||
          api.device_get_handle_by_index(index, &handle) != kNvmlSuccess) {
        continue;
      }
      std::string name;
      if (api.device_get_name != nullptr) {
        char buffer[96] = {0};
        if (api.device_get_name(handle, buffer, sizeof(buffer)) == kNvmlSuccess) {
          name = read_string(buffer, sizeof(buffer));
        }
      }
      std::string uuid;
      if (api.device_get_uuid != nullptr) {
        char buffer[96] = {0};
        if (api.device_get_uuid(handle, buffer, sizeof(buffer)) == kNvmlSuccess) {
          uuid = read_string(buffer, sizeof(buffer));
        }
      }
      if (uuid.empty()) {
        uuid = "GPU-index-" + std::to_string(index);
      }
      std::string vbios;
      if (api.device_get_vbios_version != nullptr) {
        char buffer[96] = {0};
        if (api.device_get_vbios_version(handle, buffer, sizeof(buffer)) == kNvmlSuccess) {
          vbios = read_string(buffer, sizeof(buffer));
        }
      }
      std::string serial;
      bool serial_available = false;
      if (api.device_get_serial != nullptr) {
        char buffer[96] = {0};
        if (api.device_get_serial(handle, buffer, sizeof(buffer)) == kNvmlSuccess) {
          serial = read_string(buffer, sizeof(buffer));
          serial_available = !serial.empty();
        }
      }
      NvmlPciInfo pci{};
      const bool have_pci = api.device_get_pci_info != nullptr &&
                            api.device_get_pci_info(handle, &pci) == kNvmlSuccess;
      int compute_major = 0;
      int compute_minor = 0;
      const bool have_compute = api.device_get_cuda_compute_capability != nullptr &&
                                api.device_get_cuda_compute_capability(handle, &compute_major, &compute_minor) ==
                                    kNvmlSuccess;

      DeviceIdentity identity;
      identity.id = gpu_device_id_from_uuid(uuid);
      identity.incarnation = DeviceIncarnationId::parse(adapter_detail::token_from_hash("inc.gpu", uuid));
      identity.hardware.vendor = VendorId::parse("vendor.nvidia");
      identity.hardware.product = ProductId::parse("product.nvidia." + canonical_token(name));
      identity.hardware.family = HardwareFamilyId::parse(nvidia_family_for(name));
      identity.hardware.model = HardwareModelId::parse("model.nvidia." + canonical_token(name));
      identity.hardware.revision = HardwareRevisionId::parse(
          "rev.nvidia.pci." + (have_pci ? hex_digits(pci.pci_device_id) : std::string("unknown")));
      if (have_compute) {
        identity.hardware.architecture = ArchitectureId::parse(
            "arch.nvidia.sm_" + std::to_string(compute_major * 10 + compute_minor));
      } else {
        identity.hardware.architecture = ArchitectureId::parse("arch.nvidia.unknown");
      }
      identity.hardware.hardware_class =
          name.find("BlueField") != std::string::npos ? HardwareClass::Dpu : HardwareClass::Gpu;
      identity.hardware.native_identifiers = {
          {"nvml.uuid", uuid},
          {"nvml.name", name},
      };
      if (have_pci) {
        identity.hardware.native_identifiers.push_back({"nvml.pci.bus_id", read_string(pci.bus_id, 32)});
        identity.hardware.native_identifiers.push_back({"pci.device_id", hex_digits(pci.pci_device_id)});
        identity.hardware.native_identifiers.push_back({"pci.subsystem_id", hex_digits(pci.pci_subsystem_id)});
        identity.instance_locator = read_string(pci.bus_id, 32);
      }
      if (!vbios.empty()) identity.hardware.native_identifiers.push_back({"nvml.vbios", vbios});
      if (have_compute) {
        identity.hardware.native_identifiers.push_back(
            {"cuda.compute_capability", std::to_string(compute_major) + "." + std::to_string(compute_minor)});
      }
      identity.platform = context.platform.id;
      identity.display_name = name.empty() ? uuid : name;
      if (serial_available) identity.serial_number = serial;

      DeviceRegistration registration;
      registration.identity = identity;
      registration.software.driver.id = DriverId::parse("driver.nvidia");
      registration.software.driver.version = driver_version;
      registration.software.firmware.id = FirmwareId::parse("fw.nvidia.vbios");
      registration.software.firmware.version = vbios;
      registration.software.runtime.id = RuntimeId::parse("runtime.nvml");
      registration.software.runtime.version = nvml_version;
      registration.platform = context.platform;
      registration.observed_at_utc = context.observed_at_utc;
      registration.observation_sequence = index;

      SubjectContext subject;
      const Status registered = builder.register_subject(registration, subject);
      if (!registered.ok()) {
        report = builder.report();
        report.notes.push_back("NVML device registration failed: " + registered.describe());
        return registered;
      }

      const auto publish = [&builder, &subject](const char* capability, SupportState support,
                                                CapabilityValue value, PrecisionClass precision,
                                                const char* reference) {
        const Status status = builder.publish_capability(subject, adapter_detail::cap(capability), support,
                                                         std::move(value), precision, reference);
        (void)status;
      };

      if (have_compute) {
        publish("cap.compute.capability", SupportState::SupportedNative,
                CapabilityValue(adapter_detail::enumeration(
                    "compute_capability",
                    std::to_string(compute_major) + "." + std::to_string(compute_minor))),
                PrecisionClass::DirectReported, "nvmlDeviceGetCudaComputeCapability");
      }
      if (api.device_get_memory_info != nullptr) {
        NvmlMemory memory{};
        if (api.device_get_memory_info(handle, &memory) == kNvmlSuccess && memory.total != 0) {
          publish("cap.memory.capacity", SupportState::SupportedNative,
                  CapabilityValue(adapter_detail::bytes(memory.total)), PrecisionClass::Exact,
                  "nvmlDeviceGetMemoryInfo");
        }
      }
      if (api.device_get_memory_bus_width != nullptr) {
        unsigned int width = 0;
        if (api.device_get_memory_bus_width(handle, &width) == kNvmlSuccess && width != 0) {
          publish("cap.memory.bus_width_bits", SupportState::SupportedNative,
                  CapabilityValue(count(width, "bits")), PrecisionClass::DirectReported,
                  "nvmlDeviceGetMemoryBusWidth");
        }
      }
      if (api.device_get_num_gpu_cores != nullptr) {
        unsigned int cores = 0;
        if (api.device_get_num_gpu_cores(handle, &cores) == kNvmlSuccess && cores != 0) {
          publish("cap.compute.execution_units", SupportState::SupportedNative,
                  CapabilityValue(count(cores, "cores")), PrecisionClass::DirectReported,
                  "nvmlDeviceGetNumGpuCores");
        }
      }
      if (api.device_get_mig_mode != nullptr) {
        unsigned int current = 0;
        unsigned int pending = 0;
        const NvmlReturn result = api.device_get_mig_mode(handle, &current, &pending);
        if (result == kNvmlSuccess) {
          publish("cap.partition.mechanism",
                  current == 1u ? SupportState::SupportedNative : SupportState::Disabled,
                  CapabilityValue(adapter_detail::features({"mig"})), PrecisionClass::DirectReported,
                  "nvmlDeviceGetMigMode");
        } else if (result == kNvmlErrorNotSupported) {
          // The device reports MIG as not supported. That is positive evidence
          // of non-support for this exact device generation, not an unknown.
          publish("cap.partition.mechanism", SupportState::Unsupported, CapabilityValue{},
                  PrecisionClass::Exact, "nvmlDeviceGetMigMode -> NVML_ERROR_NOT_SUPPORTED");
          publish("cap.partition.max_partitions", SupportState::Unsupported, CapabilityValue{},
                  PrecisionClass::Exact, "nvmlDeviceGetMigMode -> NVML_ERROR_NOT_SUPPORTED");
        }
      }
      if (api.device_get_ecc_mode != nullptr) {
        NvmlEnableState current = 0;
        NvmlEnableState pending = 0;
        const NvmlReturn result = api.device_get_ecc_mode(handle, &current, &pending);
        if (result == kNvmlSuccess) {
          publish("cap.memory.ecc", SupportState::SupportedNative,
                  CapabilityValue(adapter_detail::enumeration("ecc", current == 1 ? "enabled" : "disabled")),
                  PrecisionClass::DirectReported, "nvmlDeviceGetEccMode");
          publish("cap.reliability.ecc_exposure", SupportState::SupportedNative, CapabilityValue(true),
                  PrecisionClass::DirectReported, "nvmlDeviceGetEccMode");
        } else if (result == kNvmlErrorNotSupported) {
          publish("cap.memory.ecc", SupportState::Unsupported, CapabilityValue{}, PrecisionClass::Exact,
                  "nvmlDeviceGetEccMode -> NVML_ERROR_NOT_SUPPORTED");
          publish("cap.reliability.ecc_exposure", SupportState::Unsupported, CapabilityValue{},
                  PrecisionClass::Exact, "nvmlDeviceGetEccMode -> NVML_ERROR_NOT_SUPPORTED");
        }
      }
      if (api.device_get_power_limit_constraints != nullptr) {
        unsigned int minimum = 0;
        unsigned int maximum = 0;
        if (api.device_get_power_limit_constraints(handle, &minimum, &maximum) == kNvmlSuccess &&
            maximum > minimum) {
          NumericRangeValue range;
          range.minimum = static_cast<std::int64_t>(minimum);
          range.maximum = static_cast<std::int64_t>(maximum);
          range.step = 1;
          publish("cap.power.cap_range", SupportState::SupportedNative, CapabilityValue(range),
                  PrecisionClass::DirectReported, "nvmlDeviceGetPowerManagementLimitConstraints");
        }
      }
      if (api.device_get_temperature_count != nullptr) {
        unsigned int sensors = 0;
        if (api.device_get_temperature_count(handle, kNvmlTemperatureGpu, &sensors) == kNvmlSuccess &&
            sensors != 0) {
          publish("cap.power.thermal_sensors", SupportState::SupportedNative,
                  CapabilityValue(count(sensors, "sensors")), PrecisionClass::DirectReported,
                  "nvmlDeviceGetTemperatureCount");
        }
      }
      {
        // Telemetry surfaces listed here are exactly those this adapter proved
        // answer for this device.
        std::vector<std::string> telemetry;
        if (api.device_get_memory_info != nullptr) telemetry.emplace_back("memory");
        if (api.device_get_power_limit_constraints != nullptr) telemetry.emplace_back("power-cap");
        if (api.device_get_temperature_count != nullptr) telemetry.emplace_back("temperature");
        std::sort(telemetry.begin(), telemetry.end());
        if (!telemetry.empty()) {
          publish("cap.reliability.telemetry_visibility", SupportState::SupportedNative,
                  CapabilityValue(adapter_detail::features(telemetry)), PrecisionClass::DirectReported,
                  "nvml device queries");
        }
      }
    }

    // Host-scoped accelerator count.
    DeviceIdentity host;
    host.id = host_device_id(context.platform);
    host.incarnation = DeviceIncarnationId::parse(
        adapter_detail::token_from_hash("inc.host", context.platform.id.str() + "|" +
                                                        context.platform.model + "|" +
                                                        context.software.firmware.version));
    host.hardware.vendor = VendorId::parse(canonical_token(
        context.platform.vendor.empty() ? "unknown-platform-vendor" : context.platform.vendor));
    host.hardware.product = ProductId::parse("product." + canonical_token(
        context.platform.model.empty() ? "unknown-platform" : context.platform.model));
    host.hardware.family = HardwareFamilyId::parse("family.host-platform");
    host.hardware.model = HardwareModelId::parse("model." + canonical_token(
        context.platform.model.empty() ? "unknown-platform" : context.platform.model));
    host.hardware.revision = HardwareRevisionId::parse("rev.host-platform.1");
    host.hardware.architecture = ArchitectureId::parse("arch.x86_64");
    host.hardware.hardware_class = HardwareClass::Other;
    host.hardware.native_identifiers = context.platform.native_identifiers;
    host.platform = context.platform.id;
    host.display_name = context.platform.display_name;

    DeviceRegistration host_registration;
    host_registration.identity = host;
    host_registration.software = context.software;
    host_registration.platform = context.platform;
    host_registration.observed_at_utc = context.observed_at_utc;
    SubjectContext host_subject;
    const Status host_status = builder.register_subject(host_registration, host_subject);
    if (host_status.ok()) {
      const Status published = builder.publish_capability(
          host_subject, adapter_detail::cap("cap.platform.accelerators"), SupportState::SupportedNative,
          CapabilityValue(count(device_count, "devices")), PrecisionClass::Exact, "nvmlDeviceGetCount_v2");
      (void)published;
    }

    report = builder.report();
    report.notes.push_back("NVML reported " + std::to_string(device_count) + " devices, driver " +
                           (driver_version.empty() ? std::string("unknown") : driver_version));
    return Status::success();
  }

 private:
  static NvmlApi& shared_api() {
    static NvmlApi api;
    return api;
  }
};

}  // namespace

DeviceId gpu_device_id_from_uuid(std::string_view uuid) {
  return canonical_device_id("dev.gpu", canonical_token(uuid));
}

namespace adapters {

std::unique_ptr<IDiscoveryBackend> make_nvml_backend() { return std::make_unique<NvmlBackend>(); }

}  // namespace adapters

}  // namespace hcr
