// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "hcr/ids.hpp"
#include "hcr/limits.hpp"
#include "hcr/software_version.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Typed value forms. The registry never models capability as an untyped
/// string bag: every capability value is one of these forms and carries the
/// schema identity that declares its type.
enum class ValueKind {
  None = 0,
  Boolean,
  Count,
  Bytes,
  Rate,
  Bandwidth,
  LatencyClass,
  Enumeration,
  FeatureSet,
  VersionRange,
  ArchitectureSet,
  NumericRange,
  Record,
  Relationship,
  Predicate,
};

const char* to_string(ValueKind kind) noexcept;
bool value_kind_from_string(std::string_view text, ValueKind& out) noexcept;

enum class RateUnit { Hertz, KiloHertz, MegaHertz, GigaHertz, OperationsPerSecond };
enum class BandwidthUnit {
  BytesPerSecond,
  KilobytesPerSecond,
  MegabytesPerSecond,
  GigabytesPerSecond,
  TerabytesPerSecond,
  BitsPerSecond,
  KilobitsPerSecond,
  MegabitsPerSecond,
  GigabitsPerSecond,
};

/// Highest enumerator; the wire codec validates against this bound.
inline constexpr BandwidthUnit kMaxBandwidthUnit = BandwidthUnit::GigabitsPerSecond;
enum class LatencyClass { Unknown, UltraLow, Low, Medium, High, VeryHigh };
enum class CapabilityRelation { DependsOn, Requires, AlternativeTo, Supersedes, Exclusive };

const char* to_string(RateUnit unit) noexcept;
const char* to_string(BandwidthUnit unit) noexcept;
const char* to_string(LatencyClass value) noexcept;
const char* to_string(CapabilityRelation relation) noexcept;
bool rate_unit_from_string(std::string_view text, RateUnit& out) noexcept;
bool bandwidth_unit_from_string(std::string_view text, BandwidthUnit& out) noexcept;
bool latency_class_from_string(std::string_view text, LatencyClass& out) noexcept;
bool capability_relation_from_string(std::string_view text, CapabilityRelation& out) noexcept;

struct CountValue {
  std::uint64_t count = 0;
  std::string unit;  ///< optional canonical unit token, e.g. "threads"
  friend bool operator==(const CountValue& a, const CountValue& b) noexcept {
    return a.count == b.count && a.unit == b.unit;
  }
  friend bool operator<(const CountValue& a, const CountValue& b) noexcept {
    if (a.count != b.count) return a.count < b.count;
    return a.unit < b.unit;
  }
};

struct BytesValue {
  std::uint64_t bytes = 0;
  friend bool operator==(const BytesValue& a, const BytesValue& b) noexcept { return a.bytes == b.bytes; }
  friend bool operator<(const BytesValue& a, const BytesValue& b) noexcept { return a.bytes < b.bytes; }
};

struct RateValue {
  std::uint64_t magnitude = 0;
  RateUnit unit = RateUnit::Hertz;
  friend bool operator==(const RateValue& a, const RateValue& b) noexcept {
    return a.magnitude == b.magnitude && a.unit == b.unit;
  }
  friend bool operator<(const RateValue& a, const RateValue& b) noexcept {
    if (a.magnitude != b.magnitude) return a.magnitude < b.magnitude;
    return static_cast<int>(a.unit) < static_cast<int>(b.unit);
  }
};

struct BandwidthValue {
  std::uint64_t magnitude = 0;
  BandwidthUnit unit = BandwidthUnit::BytesPerSecond;
  friend bool operator==(const BandwidthValue& a, const BandwidthValue& b) noexcept {
    return a.magnitude == b.magnitude && a.unit == b.unit;
  }
  friend bool operator<(const BandwidthValue& a, const BandwidthValue& b) noexcept {
    if (a.magnitude != b.magnitude) return a.magnitude < b.magnitude;
    return static_cast<int>(a.unit) < static_cast<int>(b.unit);
  }
};

struct EnumValue {
  std::string domain;
  std::string value;
  friend bool operator==(const EnumValue& a, const EnumValue& b) noexcept {
    return a.domain == b.domain && a.value == b.value;
  }
  friend bool operator<(const EnumValue& a, const EnumValue& b) noexcept {
    if (a.domain != b.domain) return a.domain < b.domain;
    return a.value < b.value;
  }
};

/// Sorted, deduplicated canonical member set.
struct FeatureSetValue {
  std::vector<std::string> features;
  friend bool operator==(const FeatureSetValue& a, const FeatureSetValue& b) noexcept {
    return a.features == b.features;
  }
  friend bool operator<(const FeatureSetValue& a, const FeatureSetValue& b) noexcept {
    return a.features < b.features;
  }
};

struct ArchitectureSetValue {
  std::vector<ArchitectureId> architectures;
  friend bool operator==(const ArchitectureSetValue& a, const ArchitectureSetValue& b) noexcept {
    return a.architectures == b.architectures;
  }
  friend bool operator<(const ArchitectureSetValue& a, const ArchitectureSetValue& b) noexcept {
    return a.architectures < b.architectures;
  }
};

struct VersionRangeValue {
  VersionRange range;
  friend bool operator==(const VersionRangeValue& a, const VersionRangeValue& b) noexcept {
    return SoftwareVersion::compare(a.range.minimum, b.range.minimum) == 0 &&
           SoftwareVersion::compare(a.range.maximum, b.range.maximum) == 0 &&
           a.range.include_maximum == b.range.include_maximum;
  }
  friend bool operator<(const VersionRangeValue& a, const VersionRangeValue& b) noexcept;
};

struct NumericRangeValue {
  std::int64_t minimum = 0;
  std::int64_t maximum = 0;
  std::int64_t step = 1;
  friend bool operator==(const NumericRangeValue& a, const NumericRangeValue& b) noexcept {
    return a.minimum == b.minimum && a.maximum == b.maximum && a.step == b.step;
  }
  friend bool operator<(const NumericRangeValue& a, const NumericRangeValue& b) noexcept {
    if (a.minimum != b.minimum) return a.minimum < b.minimum;
    if (a.maximum != b.maximum) return a.maximum < b.maximum;
    return a.step < b.step;
  }
};

struct RelationshipValue {
  CapabilityRelation relation = CapabilityRelation::DependsOn;
  CapabilityId target;
  friend bool operator==(const RelationshipValue& a, const RelationshipValue& b) noexcept {
    return a.relation == b.relation && a.target == b.target;
  }
  friend bool operator<(const RelationshipValue& a, const RelationshipValue& b) noexcept {
    if (a.relation != b.relation) return static_cast<int>(a.relation) < static_cast<int>(b.relation);
    return a.target < b.target;
  }
};

class CapabilityValue;

/// Structured record field. The shared pointer intentionally breaks the
/// recursive type cycle while keeping records immutable once published.
struct RecordField {
  std::string name;
  std::shared_ptr<const CapabilityValue> value;
};

/// Canonically ordered structured record (fields sorted by name).
struct RecordValue {
  std::vector<RecordField> fields;
};

struct ConditionNode;
using ConditionPtr = std::shared_ptr<const ConditionNode>;

struct PredicateValue {
  ConditionPtr condition;
};

/// A typed capability value.
class CapabilityValue {
 public:
  using Storage =
      std::variant<std::monostate, bool, CountValue, BytesValue, RateValue, BandwidthValue, LatencyClass,
                   EnumValue, FeatureSetValue, VersionRangeValue, ArchitectureSetValue, NumericRangeValue,
                   RecordValue, RelationshipValue, PredicateValue>;

  CapabilityValue() = default;
  CapabilityValue(bool v) : storage_(v) {}
  CapabilityValue(CountValue v) : storage_(std::move(v)) {}
  CapabilityValue(BytesValue v) : storage_(std::move(v)) {}
  CapabilityValue(RateValue v) : storage_(std::move(v)) {}
  CapabilityValue(BandwidthValue v) : storage_(std::move(v)) {}
  CapabilityValue(LatencyClass v) : storage_(v) {}
  CapabilityValue(EnumValue v) : storage_(std::move(v)) {}
  CapabilityValue(FeatureSetValue v) : storage_(std::move(v)) {}
  CapabilityValue(VersionRangeValue v) : storage_(std::move(v)) {}
  CapabilityValue(ArchitectureSetValue v) : storage_(std::move(v)) {}
  CapabilityValue(NumericRangeValue v) : storage_(std::move(v)) {}
  CapabilityValue(RecordValue v) : storage_(std::move(v)) {}
  CapabilityValue(RelationshipValue v) : storage_(std::move(v)) {}
  CapabilityValue(PredicateValue v) : storage_(std::move(v)) {}

  [[nodiscard]] ValueKind kind() const noexcept;
  [[nodiscard]] const Storage& storage() const noexcept { return storage_; }
  /// Mutable access is used only by normalization, which canonicalizes set-like
  /// values before they are validated and stored.
  [[nodiscard]] Storage& mutable_storage() noexcept { return storage_; }
  [[nodiscard]] bool empty() const noexcept { return storage_.index() == 0; }

  /// Canonical, locale-independent rendering used by explanations, the CLI and
  /// canonical digests. Two equal values always render identically.
  [[nodiscard]] std::string describe() const;

  friend bool operator==(const CapabilityValue& a, const CapabilityValue& b) noexcept;
  friend bool operator!=(const CapabilityValue& a, const CapabilityValue& b) noexcept { return !(a == b); }
  /// Total order over values; used for deterministic comparison and listing.
  friend bool operator<(const CapabilityValue& a, const CapabilityValue& b) noexcept;

 private:
  Storage storage_;
};

/// Structural validation: value form completeness, unit tokens, set ordering
/// and bounds, record depth and field counts.
Status validate_value(const CapabilityValue& value, const RegistryLimits& limits);

/// Normalizes set-like values into canonical order and removes duplicates.
/// Returns a validation failure for forms that cannot be normalized.
Status normalize_value(CapabilityValue& value, const RegistryLimits& limits);

}  // namespace hcr
