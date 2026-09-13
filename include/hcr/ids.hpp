// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace hcr {

/// Canonical token grammar for every opaque identity the registry accepts.
///
/// The grammar is deliberately narrow: lowercase ASCII letters, digits and the
/// separators '.', '_', ':', '-', with a length bound. Narrow tokens make
/// ordering, persistence and wire encoding canonical, and they prevent backend
/// native strings (which may contain locale, whitespace or separator
/// characters) from leaking into canonical identity. Backend native strings
/// are preserved separately as provenance metadata.
bool is_canonical_token(std::string_view text) noexcept;

/// Maximum canonical token length in bytes.
inline constexpr std::size_t kMaxTokenLength = 128;

/// Strongly typed opaque identity.
///
/// Each tag corresponds to an identity boundary that the registry actually
/// enforces (stale-state detection, supersession, compatibility or evidence
/// fencing). No decorative identifiers exist.
template <typename Tag>
class Id {
 public:
  Id() = default;

  /// Parses a canonical token. Returns an invalid (empty) Id when the text is
  /// not canonical, so callers can validate without exceptions.
  static Id parse(std::string_view text) {
    if (!is_canonical_token(text)) return Id{};
    return Id{std::string(text)};
  }

  static bool is_valid(std::string_view text) noexcept { return is_canonical_token(text); }

  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] bool valid() const noexcept { return !value_.empty(); }
  [[nodiscard]] std::size_t hash() const noexcept { return std::hash<std::string>{}(value_); }

  /// Total order on the canonical token; the registry relies on this for
  /// deterministic listing, comparison and snapshot serialization.
  friend bool operator==(const Id& a, const Id& b) noexcept { return a.value_ == b.value_; }
  friend bool operator!=(const Id& a, const Id& b) noexcept { return !(a == b); }
  friend bool operator<(const Id& a, const Id& b) noexcept { return a.value_ < b.value_; }
  friend bool operator>(const Id& a, const Id& b) noexcept { return b < a; }
  friend bool operator<=(const Id& a, const Id& b) noexcept { return !(b < a); }
  friend bool operator>=(const Id& a, const Id& b) noexcept { return !(a < b); }

 private:
  explicit Id(std::string token) : value_(std::move(token)) {}
  std::string value_;
};

/// Monotonic generation counter. Generation zero means "not set"; generations
/// the registry issues start at one and never regress.
template <typename Tag>
class Gen {
 public:
  constexpr Gen() noexcept = default;
  explicit constexpr Gen(std::uint64_t value) noexcept : value_(value) {}

  static constexpr Gen none() noexcept { return Gen{}; }
  static constexpr Gen first() noexcept { return Gen{1}; }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
  [[nodiscard]] constexpr Gen next() const noexcept { return Gen{value_ + 1}; }
  [[nodiscard]] std::string str() const { return std::to_string(value_); }

  friend constexpr bool operator==(const Gen& a, const Gen& b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(const Gen& a, const Gen& b) noexcept { return !(a == b); }
  friend constexpr bool operator<(const Gen& a, const Gen& b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator>(const Gen& a, const Gen& b) noexcept { return b < a; }
  friend constexpr bool operator<=(const Gen& a, const Gen& b) noexcept { return !(b < a); }
  friend constexpr bool operator>=(const Gen& a, const Gen& b) noexcept { return !(a < b); }

 private:
  std::uint64_t value_ = 0;
};

// ---------------------------------------------------------------------------
// Identity tags. Each maps to exactly one ownership boundary.
// ---------------------------------------------------------------------------

struct VendorIdTag {};
struct ProductIdTag {};
struct HardwareFamilyIdTag {};
struct HardwareModelIdTag {};
struct HardwareRevisionIdTag {};
struct DeviceIdTag {};
struct DeviceIncarnationIdTag {};
struct ArchitectureIdTag {};
struct FirmwareIdTag {};
struct DriverIdTag {};
struct RuntimeIdTag {};
struct PlatformIdTag {};
struct CapabilityIdTag {};
struct CapabilitySchemaIdTag {};
struct QuirkIdTag {};
struct CompatibilityFactIdTag {};
struct PublisherIdTag {};

using VendorId = Id<VendorIdTag>;
using ProductId = Id<ProductIdTag>;
using HardwareFamilyId = Id<HardwareFamilyIdTag>;
using HardwareModelId = Id<HardwareModelIdTag>;
using HardwareRevisionId = Id<HardwareRevisionIdTag>;
using DeviceId = Id<DeviceIdTag>;
using DeviceIncarnationId = Id<DeviceIncarnationIdTag>;
using ArchitectureId = Id<ArchitectureIdTag>;
using FirmwareId = Id<FirmwareIdTag>;
using DriverId = Id<DriverIdTag>;
using RuntimeId = Id<RuntimeIdTag>;
using PlatformId = Id<PlatformIdTag>;
using CapabilityId = Id<CapabilityIdTag>;
using CapabilitySchemaId = Id<CapabilitySchemaIdTag>;
using QuirkId = Id<QuirkIdTag>;
using CompatibilityFactId = Id<CompatibilityFactIdTag>;
using PublisherId = Id<PublisherIdTag>;

// ---------------------------------------------------------------------------
// Generation tags. Each is a real staleness / fencing boundary.
// ---------------------------------------------------------------------------

/// Capability-evidence boundary for a physical device. Advances when the
/// device reincarnates (reset, replacement, re-enumeration) or when the
/// registry is told to invalidate the device's dynamic evidence.
struct DeviceGenerationTag {};
/// Device-side boot counter of one incarnation; advances on every observed
/// device boot/reset and is retained on evidence to explain staleness.
struct DeviceBootTag {};
struct FirmwareGenerationTag {};
struct DriverGenerationTag {};
struct RuntimeGenerationTag {};
struct PlatformGenerationTag {};
struct CapabilityGenerationTag {};
struct QuirkGenerationTag {};
struct CompatibilityGenerationTag {};
struct EvidenceGenerationTag {};
struct CoordinatorEpochTag {};
struct SnapshotGenerationTag {};
struct PublisherBootTag {};

using DeviceGeneration = Gen<DeviceGenerationTag>;
using DeviceBootId = Gen<DeviceBootTag>;
using FirmwareGeneration = Gen<FirmwareGenerationTag>;
using DriverGeneration = Gen<DriverGenerationTag>;
using RuntimeGeneration = Gen<RuntimeGenerationTag>;
using PlatformGeneration = Gen<PlatformGenerationTag>;
using CapabilityGeneration = Gen<CapabilityGenerationTag>;
using QuirkGeneration = Gen<QuirkGenerationTag>;
using CompatibilityGeneration = Gen<CompatibilityGenerationTag>;
using EvidenceGeneration = Gen<EvidenceGenerationTag>;
using CoordinatorEpoch = Gen<CoordinatorEpochTag>;
using SnapshotGeneration = Gen<SnapshotGenerationTag>;
using PublisherBootId = Gen<PublisherBootTag>;

}  // namespace hcr

namespace std {
template <typename Tag>
struct hash<hcr::Id<Tag>> {
  std::size_t operator()(const hcr::Id<Tag>& id) const noexcept { return id.hash(); }
};
template <typename Tag>
struct hash<hcr::Gen<Tag>> {
  std::size_t operator()(const hcr::Gen<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};
}  // namespace std
