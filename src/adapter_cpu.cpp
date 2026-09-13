// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// CPU and host-platform discovery. Everything published here is read directly
// from the operating system or from CPUID; nothing is inferred from a product
// name where a direct detection exists.
#include "adapter_common.hpp"
#include "hcr/adapters.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#endif

namespace hcr::adapters {
namespace {

using adapter_detail::cap;
using adapter_detail::canonical_token;
using adapter_detail::count;
using adapter_detail::enumeration;
using adapter_detail::features;

#if defined(_WIN32)

std::string registry_string(HKEY root, const char* path, const char* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
  char buffer[1024];
  DWORD size = sizeof(buffer);
  DWORD type = 0;
  const LONG status = RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS) return {};
  if (type != REG_SZ && type != REG_EXPAND_SZ) return {};
  std::size_t length = size;
  while (length > 0 && buffer[length - 1] == '\0') --length;
  return std::string(buffer, length);
}

struct CpuidRegisters {
  int values[4] = {0, 0, 0, 0};
};

CpuidRegisters cpuid(int leaf, int subleaf) {
  CpuidRegisters registers;
  __cpuidex(registers.values, leaf, subleaf);
  return registers;
}

std::string windows_build() {
  // RtlGetVersion is used instead of GetVersionEx, which is subject to
  // manifest-based compatibility shimming and would misreport the build.
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (ntdll == nullptr) return {};
  const auto function = reinterpret_cast<RtlGetVersionFn>(
      reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
  if (function == nullptr) return {};
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (function(&info) != 0) return {};
  return std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." +
         std::to_string(info.dwBuildNumber);
}

struct ProcessorTopology {
  std::size_t physical_cores = 0;
  std::size_t logical_processors = 0;
  std::size_t numa_nodes = 0;
  std::size_t l1d = 0;
  std::size_t l1i = 0;
  std::size_t l2 = 0;
  std::size_t l3 = 0;
};

bool collect_topology(ProcessorTopology& topology) {
  DWORD length = 0;
  GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
  if (length == 0) return false;
  std::vector<std::uint8_t> buffer(length);
  if (!GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                                        &length)) {
    return false;
  }
  std::size_t offset = 0;
  while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= length) {
    const auto* entry =
        reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
    if (entry->Relationship == RelationProcessorCore) {
      ++topology.physical_cores;
    } else if (entry->Relationship == RelationNumaNode) {
      ++topology.numa_nodes;
    } else if (entry->Relationship == RelationCache) {
      const CACHE_RELATIONSHIP& cache = entry->Cache;
      if (cache.Level == 1 && cache.Type == CacheData) topology.l1d += cache.CacheSize;
      else if (cache.Level == 1 && cache.Type == CacheInstruction) topology.l1i += cache.CacheSize;
      else if (cache.Level == 2) topology.l2 += cache.CacheSize;
      else if (cache.Level == 3) topology.l3 += cache.CacheSize;
    }
    if (entry->Size == 0) break;
    offset += entry->Size;
  }
  topology.logical_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return topology.physical_cores != 0;
}

void collect_isa_features(std::vector<std::string>& out) {
  const CpuidRegisters leaf1 = cpuid(1, 0);
  const CpuidRegisters leaf7 = cpuid(7, 0);
  const std::uint32_t ecx1 = static_cast<std::uint32_t>(leaf1.values[2]);
  const std::uint32_t edx1 = static_cast<std::uint32_t>(leaf1.values[3]);
  const std::uint32_t ebx7 = static_cast<std::uint32_t>(leaf7.values[1]);
  const std::uint32_t ecx7 = static_cast<std::uint32_t>(leaf7.values[2]);
  const std::uint32_t edx7 = static_cast<std::uint32_t>(leaf7.values[3]);

  const auto add = [&out](bool present, const char* name) {
    if (present) out.emplace_back(name);
  };
  add((edx1 & (1u << 26)) != 0, "sse2");
  add((ecx1 & (1u << 0)) != 0, "sse3");
  add((ecx1 & (1u << 9)) != 0, "ssse3");
  add((ecx1 & (1u << 19)) != 0, "sse4.1");
  add((ecx1 & (1u << 20)) != 0, "sse4.2");
  add((ecx1 & (1u << 22)) != 0, "movbe");
  add((ecx1 & (1u << 23)) != 0, "popcnt");
  add((ecx1 & (1u << 25)) != 0, "aes");
  add((ecx1 & (1u << 1)) != 0, "pclmulqdq");
  add((ecx1 & (1u << 12)) != 0, "fma3");
  add((ecx1 & (1u << 28)) != 0, "f16c");
  add((ecx1 & (1u << 29)) != 0, "avx");
  add((ecx1 & (1u << 30)) != 0, "rdrand");
  add((ebx7 & (1u << 3)) != 0, "bmi1");
  add((ebx7 & (1u << 5)) != 0, "avx2");
  add((ebx7 & (1u << 8)) != 0, "bmi2");
  add((ebx7 & (1u << 18)) != 0, "rdseed");
  add((ebx7 & (1u << 19)) != 0, "adx");
  add((ebx7 & (1u << 29)) != 0, "sha");
  add((ecx7 & (1u << 8)) != 0, "gfni");
  add((ecx7 & (1u << 9)) != 0, "vaes");
  add((ecx7 & (1u << 10)) != 0, "vpclmulqdq");
  add((edx7 & (1u << 24)) != 0, "amx.tile");
  add((edx7 & (1u << 25)) != 0, "amx.int8");
  add((edx7 & (1u << 22)) != 0, "amx.bf16");

  // AVX-class state is only usable when the OS enables it through XCR0.
  const std::uint64_t xcr0 = _xgetbv(0);
  const bool os_avx = (xcr0 & 0x6ull) == 0x6ull;
  const bool os_avx512 = (xcr0 & 0xE6ull) == 0xE6ull;
  const bool cpu_avx512 = (ebx7 & (1u << 16)) != 0;
  if (os_avx && (ebx7 & (1u << 5)) != 0) out.emplace_back("avx2.os-enabled");
  if (os_avx512 && cpu_avx512) out.emplace_back("avx512f");
  if (os_avx512 && cpu_avx512 && (ebx7 & (1u << 17)) != 0) out.emplace_back("avx512dq");
  if (os_avx512 && cpu_avx512 && (ebx7 & (1u << 30)) != 0) out.emplace_back("avx512bw");
  if (os_avx512 && cpu_avx512 && (ebx7 & (1u << 31)) != 0) out.emplace_back("avx512vl");
  if (os_avx512 && cpu_avx512 && (ecx7 & (1u << 1)) != 0) out.emplace_back("avx512vbmi");
  if (os_avx512 && cpu_avx512 && (ecx7 & (1u << 11)) != 0) out.emplace_back("avx512vnni");
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

std::string cpu_vendor_token(const char* vendor_string) {
  if (std::string(vendor_string).find("AuthenticAMD") != std::string::npos) return "vendor:amd";
  if (std::string(vendor_string).find("GenuineIntel") != std::string::npos) return "vendor:intel";
  return "vendor:" + canonical_token(vendor_string);
}

std::string brand_string() {
  char brand[49] = {0};
  int* registers = reinterpret_cast<int*>(brand);
  for (int leaf = 0; leaf < 3; ++leaf) {
    const CpuidRegisters result = cpuid(0x80000002 + leaf, 0);
    registers[leaf * 4 + 0] = result.values[0];
    registers[leaf * 4 + 1] = result.values[1];
    registers[leaf * 4 + 2] = result.values[2];
    registers[leaf * 4 + 3] = result.values[3];
  }
  std::string text(brand);
  while (!text.empty() && (text.front() == ' ' || text.front() == '\0')) text.erase(text.begin());
  while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) text.pop_back();
  return text;
}

#endif  // _WIN32

class CpuPlatformBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "cpu.platform"; }
  SourceClass source_class() const override { return SourceClass::OsEnumeration; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealOsReported; }

  bool available(std::string& reason) const override {
#if defined(_WIN32)
    reason.clear();
    return true;
#else
    reason = "the CPU discovery adapter is implemented for Windows hosts only";
    return false;
#endif
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
#if defined(_WIN32)
    ProcessorTopology topology;
    if (!collect_topology(topology)) {
      report = builder.report();
      report.notes.push_back("GetLogicalProcessorInformationEx did not report processor topology");
      return Status::failure(ErrorCode::Internal, "processor topology unavailable");
    }

    const std::string vendor_string = registry_string(HKEY_LOCAL_MACHINE,
                                                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                                      "VendorIdentifier");
    const std::string processor_name = registry_string(HKEY_LOCAL_MACHINE,
                                                       "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                                       "ProcessorNameString");
    const std::string brand = brand_string();
    const CpuidRegisters leaf1 = cpuid(1, 0);
    const std::uint32_t base_family = (static_cast<std::uint32_t>(leaf1.values[0]) >> 8) & 0xfu;
    const std::uint32_t base_model = (static_cast<std::uint32_t>(leaf1.values[0]) >> 4) & 0xfu;
    const std::uint32_t stepping = static_cast<std::uint32_t>(leaf1.values[0]) & 0xfu;
    const std::uint32_t extended_family = (static_cast<std::uint32_t>(leaf1.values[0]) >> 20) & 0xffu;
    const std::uint32_t extended_model = (static_cast<std::uint32_t>(leaf1.values[0]) >> 16) & 0xfu;
    const std::uint32_t display_family = base_family == 0xfu ? base_family + extended_family : base_family;
    const std::uint32_t display_model =
        (base_family == 0xfu || base_family == 0x6u) ? (extended_model << 4) + base_model : base_model;

    char family_hex[16];
    char model_hex[16];
    char stepping_hex[16];
    std::snprintf(family_hex, sizeof(family_hex), "%x", display_family);
    std::snprintf(model_hex, sizeof(model_hex), "%x", display_model);
    std::snprintf(stepping_hex, sizeof(stepping_hex), "%x", stepping);

    const std::string vendor_token = cpu_vendor_token(vendor_string.empty() ? brand.c_str()
                                                                          : vendor_string.c_str());
    const std::string model_token = canonical_token(processor_name.empty() ? brand : processor_name);

    DeviceIdentity cpu;
    cpu.id = canonical_device_id("dev.cpu", vendor_token + "|" + model_token);
    cpu.incarnation = DeviceIncarnationId::parse(
        adapter_detail::token_from_hash("inc.cpu", vendor_token + "|" + model_token + "|" +
                                                        std::to_string(topology.physical_cores)));
    cpu.hardware.vendor = VendorId::parse(vendor_token);
    cpu.hardware.product = ProductId::parse("product." + model_token);
    cpu.hardware.family = HardwareFamilyId::parse(
        "family." + canonical_token(vendor_string.empty() ? "unknown" : vendor_string));
    cpu.hardware.model = HardwareModelId::parse("model." + model_token);
    cpu.hardware.revision = HardwareRevisionId::parse(std::string("rev.") + canonical_token(vendor_token) + "." +
                                                      family_hex + "." + model_hex + "." + stepping_hex);
    cpu.hardware.architecture = ArchitectureId::parse("arch.x86_64");
    cpu.hardware.hardware_class = HardwareClass::Cpu;
    cpu.hardware.native_identifiers = {
        {"cpuid.vendor", vendor_string.empty() ? brand : vendor_string},
        {"cpuid.brand", brand},
        {"cpuid.family", family_hex},
        {"cpuid.model", model_hex},
        {"cpuid.stepping", stepping_hex},
        {"windows.processor.count", std::to_string(topology.logical_processors)},
    };
    cpu.platform = context.platform.id;
    cpu.instance_locator = std::nullopt;
    cpu.display_name = processor_name.empty() ? brand : processor_name;

    DeviceRegistration cpu_registration;
    cpu_registration.identity = cpu;
    cpu_registration.software = context.software;
    cpu_registration.platform = context.platform;
    cpu_registration.observed_at_utc = context.observed_at_utc;

    SubjectContext cpu_subject;
    const Status cpu_status = builder.register_subject(cpu_registration, cpu_subject);
    if (!cpu_status.ok()) {
      report = builder.report();
      return cpu_status;
    }

    std::vector<std::string> isa;
    collect_isa_features(isa);

    const auto publish = [&builder](const SubjectContext& subject, const char* name, SupportState support,
                                    CapabilityValue value, PrecisionClass precision,
                                    const char* reference) {
      const Status status =
          builder.publish_capability(subject, cap(name), support, std::move(value), precision, reference);
      return status;
    };

    publish(cpu_subject, "cap.compute.architecture", SupportState::SupportedNative,
            ArchitectureSetValue{{ArchitectureId::parse("arch.x86_64")}}, PrecisionClass::DirectReported,
            "cpuid vendor/family");

    {
      CapabilityValue value(adapter_detail::features(isa));
      publish(cpu_subject, "cap.platform.cpu.isa_features", SupportState::SupportedNative, std::move(value),
              PrecisionClass::Probed, "cpuid + xgetbv");
    }
    publish(cpu_subject, "cap.platform.cpu.logical_processors", SupportState::SupportedNative,
            CapabilityValue(count(topology.logical_processors, "processors")), PrecisionClass::Exact,
            "GetActiveProcessorCount");
    publish(cpu_subject, "cap.platform.cpu.physical_cores", SupportState::SupportedNative,
            CapabilityValue(count(topology.physical_cores, "cores")), PrecisionClass::Exact,
            "GetLogicalProcessorInformationEx(RelationProcessorCore)");
    if (topology.numa_nodes != 0) {
      publish(cpu_subject, "cap.memory.numa_nodes", SupportState::SupportedNative,
              CapabilityValue(count(topology.numa_nodes, "nodes")), PrecisionClass::Exact,
              "GetLogicalProcessorInformationEx(RelationNumaNode)");
    }
    if (topology.l1d != 0 || topology.l2 != 0 || topology.l3 != 0) {
      RecordValue record;
      const auto add_field = [&record](const char* name, std::uint64_t bytes_value) {
        if (bytes_value == 0) return;
        RecordField field;
        field.name = name;
        field.value = std::make_shared<const CapabilityValue>(CapabilityValue(adapter_detail::bytes(bytes_value)));
        record.fields.push_back(std::move(field));
      };
      add_field("l1d.bytes", topology.l1d);
      add_field("l1i.bytes", topology.l1i);
      add_field("l2.bytes", topology.l2);
      add_field("l3.bytes", topology.l3);
      std::sort(record.fields.begin(), record.fields.end(),
                [](const RecordField& a, const RecordField& b) { return a.name < b.name; });
      publish(cpu_subject, "cap.platform.cache_hierarchy", SupportState::SupportedNative,
              CapabilityValue(std::move(record)), PrecisionClass::Exact,
              "GetLogicalProcessorInformationEx(RelationCache)");
    }
    {
      std::vector<std::string> pages{"4k"};
      const SIZE_T large_page = GetLargePageMinimum();
      if (large_page >= (2u << 20)) pages.emplace_back("2m");
      if (large_page >= (1u << 30)) pages.emplace_back("1g");
      std::sort(pages.begin(), pages.end());
      publish(cpu_subject, "cap.platform.page_sizes", SupportState::SupportedNative,
              CapabilityValue(adapter_detail::features(pages)), PrecisionClass::DirectReported,
              "GetSystemInfo + GetLargePageMinimum");
    }

    // Host platform subject: OS/kernel truth belongs to the platform, not to
    // the processor.
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
    if (!host_status.ok()) {
      report = builder.report();
      return host_status;
    }

    const std::string build = windows_build();
    publish(host_subject, "cap.platform.os", SupportState::SupportedNative,
            CapabilityValue(enumeration("os", "windows")), PrecisionClass::DirectReported, "RtlGetVersion");
    if (!build.empty()) {
      publish(host_subject, "cap.platform.kernel", SupportState::SupportedNative,
              CapabilityValue(enumeration("kernel", build)), PrecisionClass::DirectReported, "RtlGetVersion");
    }
    report = builder.report();
    return Status::success();
#else
    report = builder.report();
    report.notes.push_back("unsupported host platform");
    return Status::success();
#endif
  }
};

}  // namespace

Status probe_host_platform(PlatformIdentity& platform, EnvironmentContext& environment,
                           DeviceSoftwareState& software, std::string& detail) {
#if defined(_WIN32)
  const std::string bios_vendor =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSVendor");
  const std::string bios_version =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSVersion");
  const std::string bios_date =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BIOSReleaseDate");
  const std::string board_product =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardProduct");
  const std::string board_vendor =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardManufacturer");
  const std::string system_product =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName");
  const std::string system_vendor =
      registry_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer");

  char computer_name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
  DWORD computer_name_length = MAX_COMPUTERNAME_LENGTH + 1;
  const bool have_name = GetComputerNameA(computer_name, &computer_name_length) != 0;

  const std::string host_token = adapter_detail::token_from_hash(
      "platform.host", std::string(have_name ? computer_name : "unnamed") + "|" + system_vendor + "|" +
                           system_product + "|" + board_product + "|" + bios_version);
  platform.id = PlatformId::parse(host_token);
  platform.display_name = system_product.empty() ? board_product : system_product;
  platform.vendor = system_vendor.empty() ? board_vendor : system_vendor;
  platform.model = board_product.empty() ? system_product : board_product;
  platform.architecture = ArchitectureId::parse("arch.x86_64");
  platform.native_identifiers = {
      {"smbios.bios.vendor", bios_vendor},
      {"smbios.bios.version", bios_version},
      {"smbios.bios.release_date", bios_date},
      {"smbios.board.product", board_product},
      {"smbios.system.product", system_product},
  };
  software.firmware.id = FirmwareId::parse(
      "fw." + canonical_token(bios_vendor.empty() ? "unknown-bios-vendor" : bios_vendor));
  software.firmware.version = bios_version;
  software.firmware.build = bios_date;
  const std::string build = windows_build();
  software.runtime.id = RuntimeId::parse("runtime.windows");
  software.runtime.version = build;
  environment.set_os("windows");
  if (!build.empty()) environment.set_kernel_version(build);
  detail = "platform " + platform.id.str() + " (" + platform.display_name + "), firmware " +
           (bios_version.empty() ? std::string("unknown") : bios_version);
  return Status::success();
#else
  (void)platform;
  (void)environment;
  (void)software;
  detail = "host platform probing is implemented for Windows hosts only";
  return Status::failure(ErrorCode::NotFound, detail);
#endif
}

std::unique_ptr<IDiscoveryBackend> make_cpu_platform_backend() {
  return std::make_unique<CpuPlatformBackend>();
}

}  // namespace hcr::adapters
