// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/provenance.hpp"

#include <string>

namespace hcr {
namespace {

bool equals_ci(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

}  // namespace

const char* to_string(SourceClass value) noexcept {
  switch (value) {
    case SourceClass::Unspecified: return "unspecified";
    case SourceClass::SyntheticBackend: return "synthetic-backend";
    case SourceClass::ImportedManifest: return "imported-manifest";
    case SourceClass::StaticCuratedDatabase: return "static-curated-database";
    case SourceClass::AcpiSmbios: return "acpi-smbios";
    case SourceClass::PciConfiguration: return "pci-configuration";
    case SourceClass::OsEnumeration: return "os-enumeration";
    case SourceClass::NicDriverApi: return "nic-driver-api";
    case SourceClass::CxlEnumeration: return "cxl-enumeration";
    case SourceClass::RocmSmi: return "rocm-smi";
    case SourceClass::Nvml: return "nvml";
    case SourceClass::CudaRuntime: return "cuda-runtime";
    case SourceClass::RuntimeProbe: return "runtime-probe";
    case SourceClass::FirmwareQuery: return "firmware-query";
    case SourceClass::DriverQuery: return "driver-query";
    case SourceClass::LiveVendorApi: return "live-vendor-api";
    case SourceClass::HardwareProbe: return "hardware-probe";
  }
  return "unspecified";
}

bool source_class_from_string(std::string_view text, SourceClass& out) noexcept {
  static const SourceClass kAll[] = {
      SourceClass::Unspecified,      SourceClass::SyntheticBackend, SourceClass::ImportedManifest,
      SourceClass::StaticCuratedDatabase, SourceClass::AcpiSmbios, SourceClass::PciConfiguration,
      SourceClass::OsEnumeration,    SourceClass::NicDriverApi,    SourceClass::CxlEnumeration,
      SourceClass::RocmSmi,          SourceClass::Nvml,            SourceClass::CudaRuntime,
      SourceClass::RuntimeProbe,     SourceClass::FirmwareQuery,   SourceClass::DriverQuery,
      SourceClass::LiveVendorApi,    SourceClass::HardwareProbe,
  };
  for (const SourceClass candidate : kAll) {
    if (equals_ci(text, to_string(candidate))) {
      out = candidate;
      return true;
    }
  }
  return false;
}

int source_authority(SourceClass value) noexcept {
  switch (value) {
    case SourceClass::SyntheticBackend: return 1;
    case SourceClass::Unspecified: return 2;
    case SourceClass::ImportedManifest: return 3;
    case SourceClass::StaticCuratedDatabase: return 4;
    case SourceClass::AcpiSmbios: return 5;
    case SourceClass::PciConfiguration: return 6;
    case SourceClass::OsEnumeration: return 7;
    case SourceClass::NicDriverApi: return 8;
    case SourceClass::CxlEnumeration: return 9;
    case SourceClass::RocmSmi: return 10;
    case SourceClass::Nvml: return 11;
    case SourceClass::CudaRuntime: return 12;
    case SourceClass::RuntimeProbe: return 13;
    case SourceClass::FirmwareQuery: return 14;
    case SourceClass::DriverQuery: return 15;
    case SourceClass::LiveVendorApi: return 16;
    case SourceClass::HardwareProbe: return 17;
  }
  return 0;
}

const char* to_string(ProvenanceClass value) noexcept {
  switch (value) {
    case ProvenanceClass::RealLiveHardware: return "REAL_LIVE_HARDWARE";
    case ProvenanceClass::RealOsReported: return "REAL_OS_REPORTED";
    case ProvenanceClass::RealVendorApi: return "REAL_VENDOR_API";
    case ProvenanceClass::RealRuntimeProbe: return "REAL_RUNTIME_PROBE";
    case ProvenanceClass::CuratedStatic: return "CURATED_STATIC";
    case ProvenanceClass::Derived: return "DERIVED";
    case ProvenanceClass::Imported: return "IMPORTED";
    case ProvenanceClass::Synthetic: return "SYNTHETIC";
    case ProvenanceClass::UnknownSource: return "UNKNOWN_SOURCE";
  }
  return "UNKNOWN_SOURCE";
}

bool provenance_class_from_string(std::string_view text, ProvenanceClass& out) noexcept {
  static const ProvenanceClass kAll[] = {
      ProvenanceClass::RealLiveHardware, ProvenanceClass::RealOsReported, ProvenanceClass::RealVendorApi,
      ProvenanceClass::RealRuntimeProbe, ProvenanceClass::CuratedStatic,  ProvenanceClass::Derived,
      ProvenanceClass::Imported,         ProvenanceClass::Synthetic,      ProvenanceClass::UnknownSource,
  };
  for (const ProvenanceClass candidate : kAll) {
    if (equals_ci(text, to_string(candidate))) {
      out = candidate;
      return true;
    }
  }
  return false;
}

bool provenance_is_real(ProvenanceClass value) noexcept {
  switch (value) {
    case ProvenanceClass::RealLiveHardware:
    case ProvenanceClass::RealOsReported:
    case ProvenanceClass::RealVendorApi:
    case ProvenanceClass::RealRuntimeProbe:
      return true;
    default:
      return false;
  }
}

bool provenance_is_durable(ProvenanceClass value) noexcept {
  switch (value) {
    case ProvenanceClass::CuratedStatic:
    case ProvenanceClass::Imported:
    case ProvenanceClass::Derived:
      return true;
    default:
      return false;
  }
}

ProvenanceClass default_provenance_for(SourceClass value) noexcept {
  switch (value) {
    case SourceClass::SyntheticBackend: return ProvenanceClass::Synthetic;
    case SourceClass::ImportedManifest: return ProvenanceClass::Imported;
    case SourceClass::StaticCuratedDatabase: return ProvenanceClass::CuratedStatic;
    case SourceClass::AcpiSmbios:
    case SourceClass::PciConfiguration:
    case SourceClass::OsEnumeration:
      return ProvenanceClass::RealOsReported;
    case SourceClass::NicDriverApi:
    case SourceClass::CxlEnumeration:
    case SourceClass::RocmSmi:
    case SourceClass::Nvml:
    case SourceClass::CudaRuntime:
      return ProvenanceClass::RealVendorApi;
    case SourceClass::RuntimeProbe:
    case SourceClass::FirmwareQuery:
    case SourceClass::DriverQuery:
      return ProvenanceClass::RealRuntimeProbe;
    case SourceClass::LiveVendorApi:
    case SourceClass::HardwareProbe:
      return ProvenanceClass::RealLiveHardware;
    case SourceClass::Unspecified:
      return ProvenanceClass::UnknownSource;
  }
  return ProvenanceClass::UnknownSource;
}

int provenance_strength(ProvenanceClass value) noexcept {
  switch (value) {
    case ProvenanceClass::Synthetic: return 0;
    case ProvenanceClass::UnknownSource: return 1;
    case ProvenanceClass::Imported: return 2;
    case ProvenanceClass::CuratedStatic: return 3;
    case ProvenanceClass::Derived: return 4;
    case ProvenanceClass::RealOsReported: return 5;
    case ProvenanceClass::RealVendorApi: return 6;
    case ProvenanceClass::RealRuntimeProbe: return 7;
    case ProvenanceClass::RealLiveHardware: return 8;
  }
  return 1;
}

bool provenance_allowed_for(ProvenanceClass provenance, SourceClass source) noexcept {
  // A source may weaken its claim but may never strengthen it. A curated
  // database may not claim live device proof, an operating-system enumeration
  // may not claim an executed device probe, and the synthetic backend may not
  // claim anything real.
  const ProvenanceClass implied = default_provenance_for(source);
  return provenance_strength(provenance) <= provenance_strength(implied);
}

const char* to_string(PublisherState value) noexcept {
  switch (value) {
    case PublisherState::Unknown: return "unknown";
    case PublisherState::Registered: return "registered";
    case PublisherState::Fenced: return "fenced";
    case PublisherState::Dead: return "dead";
  }
  return "unknown";
}

StalenessFlag& operator|=(StalenessFlag& a, StalenessFlag b) noexcept {
  a = a | b;
  return a;
}

bool is_stale(StalenessFlag flags) noexcept { return flags != StalenessFlag::None; }

std::string describe_staleness(StalenessFlag flags) {
  if (flags == StalenessFlag::None) return "current";
  std::string out;
  const auto append = [&out](const char* text) {
    if (!out.empty()) out += ",";
    out += text;
  };
  const std::uint32_t raw = static_cast<std::uint32_t>(flags);
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DeviceGenerationSuperseded)) append("device-generation-superseded");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DeviceBootChanged)) append("device-boot-changed");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DriverGenerationChanged)) append("driver-generation-changed");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::FirmwareGenerationChanged)) append("firmware-generation-changed");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::RuntimeGenerationChanged)) append("runtime-generation-changed");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::PlatformGenerationChanged)) append("platform-generation-changed");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::PublisherFenced)) append("publisher-fenced");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::EpochSuperseded)) append("epoch-superseded");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::SupersededByNewerEvidence)) append("superseded-by-newer-evidence");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::Withdrawn)) append("withdrawn");
  if (raw & static_cast<std::uint32_t>(StalenessFlag::RequiresRevalidation)) append("requires-revalidation");
  return out;
}

const char* staleness_code(StalenessFlag flags) noexcept {
  const std::uint32_t raw = static_cast<std::uint32_t>(flags);
  if (raw == 0) return "current";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::Withdrawn)) return "withdrawn";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::RequiresRevalidation)) return "revalidation-required";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DeviceGenerationSuperseded)) return "stale-device-generation";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DeviceBootChanged)) return "stale-device-boot";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::DriverGenerationChanged)) return "stale-driver-generation";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::FirmwareGenerationChanged)) return "stale-firmware-generation";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::RuntimeGenerationChanged)) return "stale-runtime-generation";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::PlatformGenerationChanged)) return "stale-platform-generation";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::PublisherFenced)) return "publisher-fenced";
  if (raw & static_cast<std::uint32_t>(StalenessFlag::EpochSuperseded)) return "stale-epoch";
  return "superseded";
}

}  // namespace hcr
