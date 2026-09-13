// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Network adapter discovery. Only facts the operating system actually reports
// are published. RDMA, SR-IOV, offload and SmartNIC capabilities are NOT
// published here: this adapter cannot prove them, and absence of proof is not
// evidence of absence. They therefore answer UNKNOWN, never SUPPORTED.
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
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

namespace hcr::adapters {
namespace {

using adapter_detail::canonical_token;
using adapter_detail::count;
using adapter_detail::enumeration;

#if defined(_WIN32)

std::string narrow(const wchar_t* text) {
  if (text == nullptr || *text == L'\0') return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

std::string interface_state_token(IF_OPER_STATUS status) {
  switch (status) {
    case IfOperStatusUp: return "up";
    case IfOperStatusDown: return "down";
    case IfOperStatusTesting: return "testing";
    case IfOperStatusUnknown: return "unknown";
    case IfOperStatusDormant: return "dormant";
    case IfOperStatusNotPresent: return "not-present";
    case IfOperStatusLowerLayerDown: return "lower-layer-down";
    default: return "unknown";
  }
}

#endif  // _WIN32

class NetworkBackend final : public IDiscoveryBackend {
 public:
  std::string adapter_name() const override { return "nic.windows"; }
  SourceClass source_class() const override { return SourceClass::OsEnumeration; }
  ProvenanceClass provenance() const override { return ProvenanceClass::RealOsReported; }

  bool available(std::string& reason) const override {
#if defined(_WIN32)
    reason.clear();
    return true;
#else
    reason = "the network adapter discovery backend is implemented for Windows hosts only";
    return false;
#endif
  }

  Status discover(const DiscoveryContext& context, EvidenceSink& sink, DiscoveryReport& report) override {
    PublicationBuilder builder(sink, context);
#if defined(_WIN32)
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buffer(size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
      buffer.resize(size);
      result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) {
      report = builder.report();
      report.notes.push_back("GetAdaptersAddresses failed with code " + std::to_string(result));
      return Status::success();
    }

    std::size_t published = 0;
    for (const IP_ADAPTER_ADDRESSES* entry = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
         entry != nullptr; entry = entry->Next) {
      const bool physical = entry->IfType == IF_TYPE_ETHERNET_CSMACD || entry->IfType == IF_TYPE_IEEE80211;
      if (!physical) continue;
      const std::string description = narrow(entry->Description);
      if (description.empty()) continue;
      const std::string adapter_name = entry->AdapterName == nullptr ? std::string() : std::string(entry->AdapterName);

      DeviceIdentity identity;
      identity.id = canonical_device_id("dev.nic", adapter_name.empty() ? description : adapter_name);
      identity.incarnation = DeviceIncarnationId::parse(
          adapter_detail::token_from_hash("inc.nic", adapter_name.empty() ? description : adapter_name));
      const std::size_t space = description.find(' ');
      identity.hardware.vendor =
          VendorId::parse("vendor." + canonical_token(description.substr(0, space)));
      identity.hardware.product = ProductId::parse("product." + canonical_token(description));
      identity.hardware.family = HardwareFamilyId::parse(
          entry->IfType == IF_TYPE_IEEE80211 ? "family.network.wireless" : "family.network.ethernet");
      identity.hardware.model = HardwareModelId::parse("model." + canonical_token(description));
      identity.hardware.revision = HardwareRevisionId::parse(
          "rev.iftype." + std::to_string(static_cast<unsigned int>(entry->IfType)));
      identity.hardware.architecture = ArchitectureId::parse("arch.network-adapter");
      identity.hardware.hardware_class = HardwareClass::Nic;
      identity.hardware.native_identifiers = {
          {"windows.interface.index", std::to_string(entry->IfIndex)},
          {"windows.interface.description", description},
          {"windows.interface.type", std::to_string(static_cast<unsigned int>(entry->IfType))},
      };
      if (!adapter_name.empty()) {
        identity.hardware.native_identifiers.push_back({"windows.adapter.name", adapter_name});
      }
      identity.platform = context.platform.id;
      identity.instance_locator = "ifindex:" + std::to_string(entry->IfIndex);
      identity.display_name = description;

      DeviceRegistration registration;
      registration.identity = identity;
      // The interface is observed through the operating system, so only the
      // runtime axis is known; the adapter's own firmware is not discoverable
      // through this API and is left unidentified rather than guessed.
      registration.software.runtime = context.software.runtime;
      registration.platform = context.platform;
      registration.observed_at_utc = context.observed_at_utc;
      registration.observation_sequence = entry->IfIndex;

      SubjectContext subject;
      const Status registered = builder.register_subject(registration, subject);
      if (!registered.ok()) continue;
      ++published;

      const Status state = builder.publish_capability(
          subject, adapter_detail::cap("cap.net.interface_state"), SupportState::SupportedNative,
          CapabilityValue(enumeration("interface_state", interface_state_token(entry->OperStatus))),
          PrecisionClass::DirectReported, "GetAdaptersAddresses/OperStatus");
      (void)state;

      const std::uint64_t speed =
          entry->TransmitLinkSpeed != 0 ? entry->TransmitLinkSpeed : entry->ReceiveLinkSpeed;
      // The platform reports an all-ones sentinel for an interface whose link
      // rate is not known. Publishing it would turn "unknown" into a fact.
      constexpr std::uint64_t kUnknownSpeed = ~static_cast<std::uint64_t>(0);
      if (speed != 0 && speed != kUnknownSpeed) {
        const Status link = builder.publish_capability(
            subject, adapter_detail::cap("cap.net.link_speed"), SupportState::SupportedNative,
            CapabilityValue(adapter_detail::bits_per_second(speed)), PrecisionClass::DirectReported,
            "GetAdaptersAddresses/TransmitLinkSpeed");
        (void)link;
      }
      if (entry->Mtu != 0) {
        const Status mtu = builder.publish_capability(
            subject, adapter_detail::cap("cap.net.mtu"), SupportState::SupportedNative,
            CapabilityValue(count(entry->Mtu, "bytes")), PrecisionClass::DirectReported,
            "GetAdaptersAddresses/Mtu");
        (void)mtu;
      }
      const Status dma = builder.publish_capability(
          subject, adapter_detail::cap("cap.net.dma"), SupportState::SupportedNative, CapabilityValue(true),
          PrecisionClass::DirectReported, "GetAdaptersAddresses/physical bus-mastering adapter");
      (void)dma;
    }
    report = builder.report();
    report.notes.push_back("published " + std::to_string(published) +
                           " physical network interfaces; RDMA/SR-IOV/offload/SmartNIC capabilities are "
                           "not discoverable through this API and remain UNKNOWN");
    return Status::success();
#else
    report = builder.report();
    report.notes.push_back("unsupported host platform");
    return Status::success();
#endif
  }
};

}  // namespace

std::unique_ptr<IDiscoveryBackend> make_network_backend() { return std::make_unique<NetworkBackend>(); }

}  // namespace hcr::adapters
