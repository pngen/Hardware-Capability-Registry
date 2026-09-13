// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// CUDA driver-API discovery adapter.
//
// This adapter is a second, independent live source for the same NVIDIA GPU
// identities that NVML reports: it resolves the identical vendor UUID to the
// identical canonical device identity, so the registry corroborates two live
// sources instead of creating duplicate subjects. The curated architecture
// table below is published separately, with CURATED provenance and CURATED
// precision, because it is architecture knowledge and not device proof.
#include <algorithm>
#include <cstdint>
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
using adapter_detail::enumeration;

// --- minimal, layout-stable CUDA driver API subset --------------------------
using CuResult = int;
using CuDevice = int;

struct CuUuid {
  char bytes[16];
};

constexpr CuResult kCuSuccess = 0;
constexpr int kAttributeMaxThreadsPerBlock = 1;
constexpr int kAttributeMaxSharedMemoryPerBlock = 8;
constexpr int kAttributeWarpSize = 10;
constexpr int kAttributeMultiprocessorCount = 16;
constexpr int kAttributeCanMapHostMemory = 19;
constexpr int kAttributeConcurrentKernels = 31;
constexpr int kAttributeUnifiedAddressing = 41;
constexpr int kAttributeComputeCapabilityMajor = 75;
constexpr int kAttributeComputeCapabilityMinor = 76;
constexpr int kAttributeManagedMemory = 83;
constexpr int kAttributeCooperativeLaunch = 95;

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

struct CudaApi {
  LibraryHandle handle = nullptr;
  CuResult (*init)(unsigned int) = nullptr;
  CuResult (*driver_get_version)(int*) = nullptr;
  CuResult (*device_get_count)(int*) = nullptr;
  CuResult (*device_get)(CuDevice*, int) = nullptr;
  CuResult (*device_get_name)(char*, int, CuDevice) = nullptr;
  CuResult (*device_get_uuid)(CuUuid*, CuDevice) = nullptr;
  CuResult (*device_get_attribute)(int*, int, CuDevice) = nullptr;
  bool loaded = false;
  bool initialized = false;

  bool start() {
    if (initialized) return true;
    if (!loaded) {
      loaded = true;
      handle = open_library("nvcuda.dll");
      if (handle == nullptr) handle = open_library("libcuda.so.1");
      if (handle == nullptr) return false;
      init = reinterpret_cast<CuResult (*)(unsigned int)>(library_symbol(handle, "cuInit"));
      driver_get_version = reinterpret_cast<CuResult (*)(int*)>(library_symbol(handle, "cuDriverGetVersion"));
      device_get_count = reinterpret_cast<CuResult (*)(int*)>(library_symbol(handle, "cuDeviceGetCount"));
      device_get = reinterpret_cast<CuResult (*)(CuDevice*, int)>(library_symbol(handle, "cuDeviceGet"));
      device_get_name = reinterpret_cast<CuResult (*)(char*, int, CuDevice)>(library_symbol(handle, "cuDeviceGetName"));
      device_get_uuid = reinterpret_cast<CuResult (*)(CuUuid*, CuDevice)>(library_symbol(handle, "cuDeviceGetUuid"));
      device_get_attribute =
          reinterpret_cast<CuResult (*)(int*, int, CuDevice)>(library_symbol(handle, "cuDeviceGetAttribute"));
    }
    if (init == nullptr || device_get_count == nullptr) return false;
    if (init(0) != kCuSuccess) return false;
    initialized = true;
    return true;
  }
};

/// Curated architecture knowledge: which data types an architecture defines.
///
/// This table is deliberately conservative. It is published with CURATED
/// provenance and CURATED precision, so it can never masquerade as live device
/// proof, and it is superseded by any live probe that contradicts it. Types
/// that the table does not assert stay UNKNOWN for the device.
struct CuratedArchitecture {
  const char* architecture;  // arch.nvidia.sm_NN
  std::vector<const char*> datatypes;
};

const std::vector<CuratedArchitecture>& curated_architectures() {
  static const std::vector<CuratedArchitecture> kTable = {
      {"arch.nvidia.sm_70", {"fp16", "int8"}},
      {"arch.nvidia.sm_75", {"fp16", "int4", "int8"}},
      {"arch.nvidia.sm_80", {"bf16", "fp16", "int4", "int8", "tf32"}},
      {"arch.nvidia.sm_86", {"bf16", "fp16", "int4", "int8", "tf32"}},
      {"arch.nvidia.sm_89", {"bf16", "fp16", "int4", "int8", "tf32"}},
      {"arch.nvidia.sm_90", {"bf16", "fp16", "fp8", "int4", "int8", "tf32"}},
      {"arch.nvidia.sm_100", {"bf16", "fp16", "fp8", "int4", "int8", "tf32"}},
      {"arch.nvidia.sm_120", {"bf16", "fp16", "int4", "int8", "tf32"}},
  };
  return kTable;
}

const CuratedArchitecture* find_curated(const std::string& architecture) {
  for (const CuratedArchitecture& entry : curated_architectures()) {
    if (architecture == entry.architecture) return &entry;
  }
  return nullptr;
}

class CudaRuntimeBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "cuda.driver-api"; }
  SourceClass source_class() const override { return SourceClass::CudaRuntime; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealVendorApi; }

  bool available(std::string& reason) const override {
    CudaApi& api = shared_api();
    if (!api.start()) {
      reason = "the CUDA driver library is not present or cannot be initialized on this host";
      return false;
    }
    reason.clear();
    return true;
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
    CudaApi& api = shared_api();
    if (!api.start()) {
      report = builder.report();
      report.notes.push_back("CUDA driver API unavailable");
      return Status::success();
    }

    int driver_version = 0;
    if (api.driver_get_version != nullptr && api.driver_get_version(&driver_version) == kCuSuccess) {
      const std::string version = std::to_string(driver_version / 1000) + "." +
                                  std::to_string((driver_version % 1000) / 10);
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

      DeviceRegistration registration;
      registration.identity = host;
      registration.software = context.software;
      registration.platform = context.platform;
      registration.observed_at_utc = context.observed_at_utc;
      SubjectContext subject;
      const Status registered = builder.register_subject(registration, subject);
      if (registered.ok()) {
        const Status published = builder.publish_capability(
            subject, adapter_detail::cap("cap.runtime.cuda"), SupportState::SupportedNative,
            CapabilityValue(enumeration("runtime", version)), PrecisionClass::DirectReported,
            "cuDriverGetVersion");
        (void)published;
      }
    }

    int device_count = 0;
    if (api.device_get_count == nullptr || api.device_get_count(&device_count) != kCuSuccess) {
      report = builder.report();
      report.notes.push_back("cuDeviceGetCount failed");
      return Status::success();
    }

    for (int index = 0; index < device_count; ++index) {
      CuDevice device = 0;
      if (api.device_get == nullptr || api.device_get(&device, index) != kCuSuccess) continue;
      CuUuid uuid{};
      std::string uuid_text;
      if (api.device_get_uuid != nullptr && api.device_get_uuid(&uuid, device) == kCuSuccess) {
        uuid_text = adapter_detail::format_gpu_uuid(reinterpret_cast<const std::uint8_t*>(uuid.bytes));
      }
      if (uuid_text.empty()) continue;

      char name_buffer[128] = {0};
      std::string name;
      if (api.device_get_name != nullptr && api.device_get_name(name_buffer, sizeof(name_buffer), device) == kCuSuccess) {
        name = std::string(name_buffer);
      }

      const auto attribute = [&api, device](int code) -> int {
        int value = 0;
        if (api.device_get_attribute == nullptr) return 0;
        if (api.device_get_attribute(&value, code, device) != kCuSuccess) return 0;
        return value;
      };

      const int major = attribute(kAttributeComputeCapabilityMajor);
      const int minor = attribute(kAttributeComputeCapabilityMinor);
      const std::string architecture = (major > 0)
                                           ? "arch.nvidia.sm_" + std::to_string(major * 10 + minor)
                                           : std::string("arch.nvidia.unknown");

      DeviceIdentity identity;
      identity.id = gpu_device_id_from_uuid(uuid_text);
      identity.incarnation = DeviceIncarnationId::parse(adapter_detail::token_from_hash("inc.gpu", uuid_text));
      identity.hardware.vendor = VendorId::parse("vendor.nvidia");
      identity.hardware.product = ProductId::parse("product.nvidia." + canonical_token(name));
      identity.hardware.family = HardwareFamilyId::parse("family.nvidia.cuda-device");
      identity.hardware.model = HardwareModelId::parse("model.nvidia." + canonical_token(name));
      identity.hardware.revision = HardwareRevisionId::parse("rev.nvidia.cuda-device");
      identity.hardware.architecture = ArchitectureId::parse(architecture);
      identity.hardware.hardware_class = HardwareClass::Gpu;
      identity.hardware.native_identifiers = {
          {"cuda.uuid", uuid_text},
          {"cuda.name", name},
          {"cuda.ordinal", std::to_string(index)},
      };
      identity.platform = context.platform.id;
      identity.display_name = name.empty() ? uuid_text : name;

      DeviceRegistration registration;
      registration.identity = identity;
      // Only the CUDA runtime axis is observed here. The CUDA driver API version
      // is a CUDA toolkit version, not the installed driver package version, so
      // it is deliberately not published as the device driver identity: doing
      // so would invalidate driver-qualified evidence another adapter read.
      registration.software.runtime.id = RuntimeId::parse("runtime.cuda");
      if (driver_version != 0) {
        registration.software.runtime.version =
            std::to_string(driver_version / 1000) + "." + std::to_string((driver_version % 1000) / 10);
      }
      registration.platform = context.platform;
      registration.observed_at_utc = context.observed_at_utc;
      registration.observation_sequence = static_cast<std::uint64_t>(index);

      SubjectContext subject;
      const Status registered = builder.register_subject(registration, subject);
      if (!registered.ok()) {
        report = builder.report();
        report.notes.push_back("CUDA device registration failed: " + registered.describe());
        return registered;
      }

      const auto publish = [&builder, &subject](const char* capability, SupportState support,
                                                CapabilityValue value, PrecisionClass precision,
                                                const char* reference, SourceClass source_class,
                                                ProvenanceClass provenance) {
        const Status status = builder.publish_capability(subject, adapter_detail::cap(capability), support,
                                                         std::move(value), precision, reference, nullptr,
                                                         source_class, provenance);
        (void)status;
      };

      if (major > 0) {
        publish("cap.compute.capability", SupportState::SupportedNative,
                CapabilityValue(enumeration("compute_capability",
                                            std::to_string(major) + "." + std::to_string(minor))),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(COMPUTE_CAPABILITY)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
        publish("cap.runtime.kernel_architectures", SupportState::SupportedNative,
                CapabilityValue(ArchitectureSetValue{{ArchitectureId::parse(architecture)}}),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(COMPUTE_CAPABILITY)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      const int multiprocessors = attribute(kAttributeMultiprocessorCount);
      if (multiprocessors > 0) {
        // Streaming multiprocessors are a different quantity from the shader
        // core count another vendor API reports, so they are published as their
        // own capability rather than as the same number under one name.
        publish("cap.compute.multiprocessors", SupportState::SupportedNative,
                CapabilityValue(count(static_cast<std::uint64_t>(multiprocessors), "multiprocessors")),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(MULTIPROCESSOR_COUNT)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      // The warp size is deliberately not published as a vector width: a warp is
      // a scheduling group, not a SIMD width, and the registry must not relabel
      // one quantity as another.
      if (attribute(kAttributeUnifiedAddressing) != 0) {
        publish("cap.memory.unified_addressing", SupportState::SupportedNative, CapabilityValue(true),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(UNIFIED_ADDRESSING)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      if (attribute(kAttributeCanMapHostMemory) != 0) {
        publish("cap.memory.pinned_host_interaction", SupportState::SupportedNative, CapabilityValue(true),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(CAN_MAP_HOST_MEMORY)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      if (attribute(kAttributeConcurrentKernels) != 0) {
        publish("cap.compute.concurrent_kernels", SupportState::SupportedNative, CapabilityValue(true),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(CONCURRENT_KERNELS)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      if (attribute(kAttributeCooperativeLaunch) != 0) {
        publish("cap.compute.cooperative_execution", SupportState::SupportedNative, CapabilityValue(true),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(COOPERATIVE_LAUNCH)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }
      if (attribute(kAttributeManagedMemory) != 0) {
        publish("cap.memory.managed_memory", SupportState::SupportedNative, CapabilityValue(true),
                PrecisionClass::DirectReported, "cuDeviceGetAttribute(MANAGED_MEMORY)",
                SourceClass::CudaRuntime, ProvenanceClass::RealVendorApi);
      }

      // Curated architecture knowledge, published with curated provenance.
      const CuratedArchitecture* curated = find_curated(architecture);
      if (curated != nullptr) {
        std::vector<std::string> datatypes;
        for (const char* datatype : curated->datatypes) datatypes.emplace_back(datatype);
        std::sort(datatypes.begin(), datatypes.end());
        publish("cap.compute.datatypes", SupportState::SupportedNative,
                CapabilityValue(adapter_detail::features(datatypes)), PrecisionClass::Curated,
                "curated architecture table (not live device proof)", SourceClass::StaticCuratedDatabase,
                ProvenanceClass::CuratedStatic);
      }
    }

    report = builder.report();
    report.notes.push_back("CUDA driver API reported " + std::to_string(device_count) + " devices");
    return Status::success();
  }

 private:
  static CudaApi& shared_api() {
    static CudaApi api;
    return api;
  }
};

}  // namespace

namespace adapters {

std::unique_ptr<IDiscoveryBackend> make_cuda_runtime_backend() {
  return std::make_unique<CudaRuntimeBackend>();
}

}  // namespace adapters

}  // namespace hcr
