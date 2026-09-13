// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
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
#include <cfgmgr32.h>
#include <setupapi.h>
#endif

namespace hcr::adapters {
namespace {

using adapter_detail::canonical_token;
using adapter_detail::count;

#if defined(_WIN32)

std::string property_string(HDEVINFO info, SP_DEVINFO_DATA& data, DWORD property) {
  DWORD type = 0;
  DWORD size = 0;
  SetupDiGetDeviceRegistryPropertyA(info, &data, property, &type, nullptr, 0, &size);
  if (size == 0) return {};
  std::vector<char> buffer(size + 1, 0);
  if (!SetupDiGetDeviceRegistryPropertyA(info, &data, property, &type,
                                         reinterpret_cast<PBYTE>(buffer.data()), size, nullptr)) {
    return {};
  }
  std::size_t length = 0;
  while (length < buffer.size() && buffer[length] != '\0') ++length;
  return std::string(buffer.data(), length);
}

std::vector<std::string> property_multi_string(HDEVINFO info, SP_DEVINFO_DATA& data, DWORD property) {
  DWORD type = 0;
  DWORD size = 0;
  SetupDiGetDeviceRegistryPropertyA(info, &data, property, &type, nullptr, 0, &size);
  if (size == 0) return {};
  std::vector<char> buffer(size + 2, 0);
  if (!SetupDiGetDeviceRegistryPropertyA(info, &data, property, &type,
                                         reinterpret_cast<PBYTE>(buffer.data()), size, nullptr)) {
    return {};
  }
  std::vector<std::string> values;
  std::size_t index = 0;
  while (index < buffer.size()) {
    const char* text = buffer.data() + index;
    const std::size_t length = std::strlen(text);
    if (length == 0) break;
    values.emplace_back(text, length);
    index += length + 1;
  }
  return values;
}

std::string extract_between(const std::string& text, const std::string& begin, const std::string& end) {
  const std::size_t start = text.find(begin);
  if (start == std::string::npos) return {};
  const std::size_t value_start = start + begin.size();
  const std::size_t stop = text.find(end, value_start);
  if (stop == std::string::npos) return {};
  return text.substr(value_start, stop - value_start);
}

#endif  // _WIN32

class PciTopologyBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "pci.topology"; }
  SourceClass source_class() const override { return SourceClass::PciConfiguration; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealOsReported; }

  bool available(std::string& reason) const override {
#if defined(_WIN32)
    reason.clear();
    return true;
#else
    reason = "the PCI topology adapter is implemented for Windows hosts only";
    return false;
#endif
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
    std::vector<PciDeviceRecord> devices;
    std::string detail;
    const Status enumerated = enumerate_pci_devices(devices, detail);
    if (!enumerated.ok()) {
      report = builder.report();
      report.notes.push_back(detail);
      return Status::success();
    }

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
    if (!registered.ok()) {
      report = builder.report();
      return registered;
    }

    const Status published = builder.publish_capability(
        subject, adapter_detail::cap("cap.platform.pci_devices"), SupportState::SupportedNative,
        CapabilityValue(count(devices.size(), "devices")), PrecisionClass::Exact,
        "SetupDiGetClassDevs(PCI)/SP_DEVINFO_DATA");
    (void)published;
    report = builder.report();
    report.notes.push_back("enumerated " + std::to_string(devices.size()) + " PCI functions");
    return Status::success();
  }
};

}  // namespace

Status enumerate_pci_devices(std::vector<PciDeviceRecord>& out, std::string& detail) {
  out.clear();
#if defined(_WIN32)
  HDEVINFO info = SetupDiGetClassDevsA(nullptr, "PCI", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (info == INVALID_HANDLE_VALUE) {
    detail = "SetupDiGetClassDevs failed for the PCI enumerator";
    return Status::failure(ErrorCode::IoFailure, detail);
  }
  SP_DEVINFO_DATA data{};
  data.cbSize = sizeof(data);
  for (DWORD index = 0; SetupDiEnumDeviceInfo(info, index, &data); ++index) {
    PciDeviceRecord record;
    char instance_buffer[512] = {0};
    if (SetupDiGetDeviceInstanceIdA(info, &data, instance_buffer, sizeof(instance_buffer), nullptr)) {
      record.instance_id = instance_buffer;
    }
    if (record.instance_id.empty()) continue;
    const std::string hardware_id = [&]() {
      const std::vector<std::string> identifiers =
          property_multi_string(info, data, SPDRP_HARDWAREID);
      return identifiers.empty() ? std::string() : identifiers.front();
    }();
    record.vendor_id = extract_between(hardware_id, "VEN_", "&");
    record.device_id = extract_between(hardware_id, "DEV_", "&");
    record.subsystem_id = extract_between(hardware_id, "SUBSYS_", "&");
    record.revision = extract_between(hardware_id, "REV_", "&");
    if (record.revision.empty()) {
      const std::size_t rev = hardware_id.find("REV_");
      if (rev != std::string::npos && rev + 4 <= hardware_id.size()) {
        record.revision = hardware_id.substr(rev + 4);
      }
    }
    const std::vector<std::string> compatible = property_multi_string(info, data, SPDRP_COMPATIBLEIDS);
    for (const std::string& identifier : compatible) {
      const std::size_t cc = identifier.find("CC_");
      if (cc != std::string::npos && cc + 9 <= identifier.size()) {
        record.class_code = identifier.substr(cc + 3, 6);
        break;
      }
    }
    record.description = property_string(info, data, SPDRP_DEVICEDESC);
    out.push_back(std::move(record));
  }
  SetupDiDestroyDeviceInfoList(info);
  std::sort(out.begin(), out.end(),
            [](const PciDeviceRecord& a, const PciDeviceRecord& b) { return a.instance_id < b.instance_id; });
  detail = "enumerated " + std::to_string(out.size()) + " PCI functions";
  return Status::success();
#else
  detail = "PCI enumeration is implemented for Windows hosts only";
  return Status::failure(ErrorCode::NotFound, detail);
#endif
}

std::unique_ptr<IDiscoveryBackend> make_pci_topology_backend() {
  return std::make_unique<PciTopologyBackend>();
}

}  // namespace hcr::adapters
