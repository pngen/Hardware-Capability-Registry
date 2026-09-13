// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// CXL discovery. When no CXL-class device exists this adapter says so instead
// of fabricating CXL capability from generic PCI devices.
#include <algorithm>
#include <string>
#include <vector>

#include "adapter_common.hpp"
#include "hcr/adapters.hpp"

namespace hcr::adapters {
namespace {

using adapter_detail::canonical_token;
using adapter_detail::count;

bool is_cxl_class(const std::string& class_code) {
  if (class_code.size() < 4) return false;
  // Base class 05 (memory controller), subclasses 02 (CXL memory) and 10 (CXL
  // cache/accelerator) are the CXL-class codes.
  const std::string base = class_code.substr(0, 2);
  const std::string subclass = class_code.substr(2, 2);
  if (base != "05") return false;
  return subclass == "02" || subclass == "10";
}

class CxlBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "cxl.enumeration"; }
  SourceClass source_class() const override { return SourceClass::CxlEnumeration; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealOsReported; }

  bool available(std::string& reason) const override {
    reason.clear();
    return true;
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

    std::vector<PciDeviceRecord> cxl_devices;
    for (const PciDeviceRecord& device : devices) {
      if (is_cxl_class(device.class_code)) cxl_devices.push_back(device);
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

    const Status counted = builder.publish_capability(
        subject, adapter_detail::cap("cap.platform.cxl_devices"), SupportState::SupportedNative,
        CapabilityValue(count(cxl_devices.size(), "devices")), PrecisionClass::Exact,
        "SetupDiGetClassDevs(PCI)/class-code 05.02|05.10");
    (void)counted;

    if (cxl_devices.empty()) {
      // Positive evidence of absence: the platform PCI bus was enumerated and
      // no CXL-class function is present.
      const Status absent = builder.publish_capability(
          subject, adapter_detail::cap("cap.memory.cxl_exposure"), SupportState::Unsupported,
          CapabilityValue{}, PrecisionClass::Exact,
          "SetupDiGetClassDevs(PCI)/no CXL class code present");
      (void)absent;
      report = builder.report();
      report.notes.push_back("no CXL-class device is present on this platform");
      return Status::success();
    }

    for (const PciDeviceRecord& device : cxl_devices) {
      DeviceIdentity identity;
      identity.id = canonical_device_id("dev.cxl", device.instance_id);
      identity.incarnation = DeviceIncarnationId::parse(
          adapter_detail::token_from_hash("inc.cxl", device.instance_id + "|" + device.revision));
      identity.hardware.vendor = VendorId::parse("vendor.pci." + canonical_token(device.vendor_id));
      identity.hardware.product = ProductId::parse("product.pci." + canonical_token(device.device_id));
      identity.hardware.family = HardwareFamilyId::parse("family.cxl-device");
      identity.hardware.model = HardwareModelId::parse("model.pci." + canonical_token(device.device_id));
      identity.hardware.revision =
          HardwareRevisionId::parse("rev.pci." + canonical_token(device.revision.empty() ? "unknown"
                                                                                        : device.revision));
      identity.hardware.architecture = ArchitectureId::parse("arch.cxl");
      identity.hardware.hardware_class = HardwareClass::CxlDevice;
      identity.hardware.native_identifiers = {
          {"pci.instance", device.instance_id},
          {"pci.vendor_id", device.vendor_id},
          {"pci.device_id", device.device_id},
          {"pci.subsystem_id", device.subsystem_id},
          {"pci.class_code", device.class_code},
      };
      identity.platform = context.platform.id;
      identity.instance_locator = device.instance_id;
      identity.display_name = device.description.empty() ? "CXL device" : device.description;

      DeviceRegistration cxl_registration;
      cxl_registration.identity = identity;
      cxl_registration.software.runtime = context.software.runtime;
      cxl_registration.platform = context.platform;
      cxl_registration.observed_at_utc = context.observed_at_utc;
      SubjectContext cxl_subject;
      const Status status = builder.register_subject(cxl_registration, cxl_subject);
      if (!status.ok()) continue;
      const Status exposure = builder.publish_capability(
          cxl_subject, adapter_detail::cap("cap.memory.cxl_exposure"), SupportState::SupportedNative,
          CapabilityValue(true), PrecisionClass::DirectReported,
          "SetupDiGetClassDevs(PCI)/class-code 05.02|05.10");
      const Status link = builder.publish_capability(
          cxl_subject, adapter_detail::cap("cap.interconnect.cxl"), SupportState::SupportedNative,
          CapabilityValue(true), PrecisionClass::DirectReported,
          "SetupDiGetClassDevs(PCI)/class-code 05.02|05.10");
      (void)exposure;
      (void)link;
    }
    report = builder.report();
    report.notes.push_back("enumerated " + std::to_string(cxl_devices.size()) + " CXL-class devices");
    return Status::success();
  }
};

}  // namespace

std::unique_ptr<IDiscoveryBackend> make_cxl_backend() { return std::make_unique<CxlBackend>(); }

}  // namespace hcr::adapters
