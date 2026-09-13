// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/value.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "hcr/canonical.hpp"
#include "hcr/condition.hpp"

namespace hcr {
namespace {

template <typename T>
const T* alternative(const CapabilityValue& value) {
  return std::get_if<T>(&value.storage());
}

std::string join_tokens(const std::vector<std::string>& tokens) {
  std::string out = "{";
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) out += ",";
    out += tokens[i];
  }
  out += "}";
  return out;
}

Status validate_record(const RecordValue& record, const RegistryLimits& limits, std::size_t depth) {
  if (depth > limits.max_record_depth) {
    return Status::failure(ErrorCode::LimitExceeded, "record nesting exceeds depth limit");
  }
  if (record.fields.size() > limits.max_record_fields) {
    return Status::failure(ErrorCode::LimitExceeded, "record exceeds field limit");
  }
  std::string previous;
  bool first = true;
  for (const RecordField& field : record.fields) {
    if (!is_canonical_token(field.name)) {
      return Status::failure(ErrorCode::InvalidValue, "record field name is not a canonical token");
    }
    if (!first && !(previous < field.name)) {
      return Status::failure(ErrorCode::InvalidValue, "record fields must be sorted and unique");
    }
    first = false;
    previous = field.name;
    if (field.value == nullptr) {
      return Status::failure(ErrorCode::InvalidValue, "record field has no value");
    }
    const Status status = validate_value(*field.value, limits);  // depth handled below
    if (!status.ok()) return status;
    if (field.value->kind() == ValueKind::Record) {
      const Status nested = validate_record(std::get<RecordValue>(field.value->storage()), limits, depth + 1);
      if (!nested.ok()) return nested;
    }
    if (field.value->kind() == ValueKind::Predicate) {
      const auto& predicate = std::get<PredicateValue>(field.value->storage());
      if (predicate.condition == nullptr) {
        return Status::failure(ErrorCode::InvalidCondition, "predicate value without condition");
      }
      const Status nested = validate_condition(*predicate.condition, limits);
      if (!nested.ok()) return nested;
    }
  }
  return Status::success();
}

}  // namespace

const char* to_string(ValueKind kind) noexcept {
  switch (kind) {
    case ValueKind::None: return "none";
    case ValueKind::Boolean: return "boolean";
    case ValueKind::Count: return "count";
    case ValueKind::Bytes: return "bytes";
    case ValueKind::Rate: return "rate";
    case ValueKind::Bandwidth: return "bandwidth";
    case ValueKind::LatencyClass: return "latency-class";
    case ValueKind::Enumeration: return "enumeration";
    case ValueKind::FeatureSet: return "feature-set";
    case ValueKind::VersionRange: return "version-range";
    case ValueKind::ArchitectureSet: return "architecture-set";
    case ValueKind::NumericRange: return "numeric-range";
    case ValueKind::Record: return "record";
    case ValueKind::Relationship: return "relationship";
    case ValueKind::Predicate: return "predicate";
  }
  return "none";
}

bool value_kind_from_string(std::string_view text, ValueKind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(ValueKind::Predicate); ++i) {
    const auto kind = static_cast<ValueKind>(i);
    if (equals_ascii_ci(text, to_string(kind))) {
      out = kind;
      return true;
    }
  }
  return false;
}

const char* to_string(RateUnit unit) noexcept {
  switch (unit) {
    case RateUnit::Hertz: return "Hz";
    case RateUnit::KiloHertz: return "kHz";
    case RateUnit::MegaHertz: return "MHz";
    case RateUnit::GigaHertz: return "GHz";
    case RateUnit::OperationsPerSecond: return "ops/s";
  }
  return "Hz";
}

const char* to_string(BandwidthUnit unit) noexcept {
  switch (unit) {
    case BandwidthUnit::BytesPerSecond: return "B/s";
    case BandwidthUnit::KilobytesPerSecond: return "kB/s";
    case BandwidthUnit::MegabytesPerSecond: return "MB/s";
    case BandwidthUnit::GigabytesPerSecond: return "GB/s";
    case BandwidthUnit::TerabytesPerSecond: return "TB/s";
    case BandwidthUnit::BitsPerSecond: return "bit/s";
    case BandwidthUnit::KilobitsPerSecond: return "kbit/s";
    case BandwidthUnit::MegabitsPerSecond: return "Mbit/s";
    case BandwidthUnit::GigabitsPerSecond: return "Gbit/s";
  }
  return "B/s";
}

const char* to_string(LatencyClass value) noexcept {
  switch (value) {
    case LatencyClass::Unknown: return "unknown";
    case LatencyClass::UltraLow: return "ultra-low";
    case LatencyClass::Low: return "low";
    case LatencyClass::Medium: return "medium";
    case LatencyClass::High: return "high";
    case LatencyClass::VeryHigh: return "very-high";
  }
  return "unknown";
}

const char* to_string(CapabilityRelation relation) noexcept {
  switch (relation) {
    case CapabilityRelation::DependsOn: return "depends-on";
    case CapabilityRelation::Requires: return "requires";
    case CapabilityRelation::AlternativeTo: return "alternative-to";
    case CapabilityRelation::Supersedes: return "supersedes";
    case CapabilityRelation::Exclusive: return "exclusive";
  }
  return "depends-on";
}

bool rate_unit_from_string(std::string_view text, RateUnit& out) noexcept {
  for (int i = 0; i <= static_cast<int>(RateUnit::OperationsPerSecond); ++i) {
    const auto unit = static_cast<RateUnit>(i);
    if (equals_ascii_ci(text, to_string(unit))) {
      out = unit;
      return true;
    }
  }
  return false;
}

bool bandwidth_unit_from_string(std::string_view text, BandwidthUnit& out) noexcept {
  for (int i = 0; i <= static_cast<int>(kMaxBandwidthUnit); ++i) {
    const auto unit = static_cast<BandwidthUnit>(i);
    if (equals_ascii_ci(text, to_string(unit))) {
      out = unit;
      return true;
    }
  }
  return false;
}

bool latency_class_from_string(std::string_view text, LatencyClass& out) noexcept {
  for (int i = 0; i <= static_cast<int>(LatencyClass::VeryHigh); ++i) {
    const auto value = static_cast<LatencyClass>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

bool capability_relation_from_string(std::string_view text, CapabilityRelation& out) noexcept {
  for (int i = 0; i <= static_cast<int>(CapabilityRelation::Exclusive); ++i) {
    const auto value = static_cast<CapabilityRelation>(i);
    if (equals_ascii_ci(text, to_string(value))) {
      out = value;
      return true;
    }
  }
  return false;
}

bool operator<(const VersionRangeValue& a, const VersionRangeValue& b) noexcept {
  const int min_cmp = SoftwareVersion::compare(a.range.minimum, b.range.minimum);
  if (min_cmp != 0) return min_cmp < 0;
  const int max_cmp = SoftwareVersion::compare(a.range.maximum, b.range.maximum);
  if (max_cmp != 0) return max_cmp < 0;
  return a.range.include_maximum < b.range.include_maximum;
}

ValueKind CapabilityValue::kind() const noexcept {
  switch (storage_.index()) {
    case 0: return ValueKind::None;
    case 1: return ValueKind::Boolean;
    case 2: return ValueKind::Count;
    case 3: return ValueKind::Bytes;
    case 4: return ValueKind::Rate;
    case 5: return ValueKind::Bandwidth;
    case 6: return ValueKind::LatencyClass;
    case 7: return ValueKind::Enumeration;
    case 8: return ValueKind::FeatureSet;
    case 9: return ValueKind::VersionRange;
    case 10: return ValueKind::ArchitectureSet;
    case 11: return ValueKind::NumericRange;
    case 12: return ValueKind::Record;
    case 13: return ValueKind::Relationship;
    case 14: return ValueKind::Predicate;
    default: return ValueKind::None;
  }
}

std::string CapabilityValue::describe() const {
  switch (storage_.index()) {
    case 0: return "none";
    case 1: return std::get<bool>(storage_) ? "true" : "false";
    case 2: {
      const CountValue& count = std::get<CountValue>(storage_);
      return count.unit.empty() ? std::to_string(count.count) : std::to_string(count.count) + " " + count.unit;
    }
    case 3: return std::to_string(std::get<BytesValue>(storage_).bytes) + " B";
    case 4: {
      const RateValue& rate = std::get<RateValue>(storage_);
      return std::to_string(rate.magnitude) + " " + to_string(rate.unit);
    }
    case 5: {
      const BandwidthValue& bandwidth = std::get<BandwidthValue>(storage_);
      return std::to_string(bandwidth.magnitude) + " " + to_string(bandwidth.unit);
    }
    case 6: return to_string(std::get<LatencyClass>(storage_));
    case 7: {
      const EnumValue& value = std::get<EnumValue>(storage_);
      return value.domain + ":" + value.value;
    }
    case 8: return join_tokens(std::get<FeatureSetValue>(storage_).features);
    case 9: return std::get<VersionRangeValue>(storage_).range.describe();
    case 10: {
      const ArchitectureSetValue& set = std::get<ArchitectureSetValue>(storage_);
      std::vector<std::string> tokens;
      tokens.reserve(set.architectures.size());
      for (const ArchitectureId& architecture : set.architectures) tokens.push_back(architecture.str());
      return join_tokens(tokens);
    }
    case 11: {
      const NumericRangeValue& range = std::get<NumericRangeValue>(storage_);
      return "[" + std::to_string(range.minimum) + "," + std::to_string(range.maximum) + "] step " +
             std::to_string(range.step);
    }
    case 12: {
      const RecordValue& record = std::get<RecordValue>(storage_);
      std::string out = "{";
      for (std::size_t i = 0; i < record.fields.size(); ++i) {
        if (i != 0) out += ",";
        out += record.fields[i].name;
        out += "=";
        out += record.fields[i].value == nullptr ? std::string("none") : record.fields[i].value->describe();
      }
      out += "}";
      return out;
    }
    case 13: {
      const RelationshipValue& relationship = std::get<RelationshipValue>(storage_);
      return std::string(to_string(relationship.relation)) + ":" + relationship.target.str();
    }
    case 14: {
      const PredicateValue& predicate = std::get<PredicateValue>(storage_);
      return std::string("if ") + describe_condition(predicate.condition.get());
    }
    default: return "none";
  }
}

bool operator==(const CapabilityValue& a, const CapabilityValue& b) noexcept {
  if (a.storage_.index() != b.storage_.index()) return false;
  switch (a.storage_.index()) {
    case 0: return true;
    case 1: return std::get<bool>(a.storage_) == std::get<bool>(b.storage_);
    case 2: return std::get<CountValue>(a.storage_) == std::get<CountValue>(b.storage_);
    case 3: return std::get<BytesValue>(a.storage_) == std::get<BytesValue>(b.storage_);
    case 4: return std::get<RateValue>(a.storage_) == std::get<RateValue>(b.storage_);
    case 5: return std::get<BandwidthValue>(a.storage_) == std::get<BandwidthValue>(b.storage_);
    case 6: return std::get<LatencyClass>(a.storage_) == std::get<LatencyClass>(b.storage_);
    case 7: return std::get<EnumValue>(a.storage_) == std::get<EnumValue>(b.storage_);
    case 8: return std::get<FeatureSetValue>(a.storage_) == std::get<FeatureSetValue>(b.storage_);
    case 9: return std::get<VersionRangeValue>(a.storage_) == std::get<VersionRangeValue>(b.storage_);
    case 10: return std::get<ArchitectureSetValue>(a.storage_) == std::get<ArchitectureSetValue>(b.storage_);
    case 11: return std::get<NumericRangeValue>(a.storage_) == std::get<NumericRangeValue>(b.storage_);
    case 12: {
      const RecordValue& left = std::get<RecordValue>(a.storage_);
      const RecordValue& right = std::get<RecordValue>(b.storage_);
      if (left.fields.size() != right.fields.size()) return false;
      for (std::size_t i = 0; i < left.fields.size(); ++i) {
        if (left.fields[i].name != right.fields[i].name) return false;
        const bool left_null = left.fields[i].value == nullptr;
        const bool right_null = right.fields[i].value == nullptr;
        if (left_null != right_null) return false;
        if (!left_null && !(*left.fields[i].value == *right.fields[i].value)) return false;
      }
      return true;
    }
    case 13: return std::get<RelationshipValue>(a.storage_) == std::get<RelationshipValue>(b.storage_);
    case 14: {
      const PredicateValue& left = std::get<PredicateValue>(a.storage_);
      const PredicateValue& right = std::get<PredicateValue>(b.storage_);
      return describe_condition(left.condition.get()) == describe_condition(right.condition.get());
    }
    default: return false;
  }
}

bool operator<(const CapabilityValue& a, const CapabilityValue& b) noexcept {
  if (a.storage_.index() != b.storage_.index()) return a.storage_.index() < b.storage_.index();
  switch (a.storage_.index()) {
    case 0: return false;
    case 1: return static_cast<int>(std::get<bool>(a.storage_)) < static_cast<int>(std::get<bool>(b.storage_));
    case 2: return std::get<CountValue>(a.storage_) < std::get<CountValue>(b.storage_);
    case 3: return std::get<BytesValue>(a.storage_) < std::get<BytesValue>(b.storage_);
    case 4: return std::get<RateValue>(a.storage_) < std::get<RateValue>(b.storage_);
    case 5: return std::get<BandwidthValue>(a.storage_) < std::get<BandwidthValue>(b.storage_);
    case 6: return static_cast<int>(std::get<LatencyClass>(a.storage_)) <
                   static_cast<int>(std::get<LatencyClass>(b.storage_));
    case 7: return std::get<EnumValue>(a.storage_) < std::get<EnumValue>(b.storage_);
    case 8: return std::get<FeatureSetValue>(a.storage_) < std::get<FeatureSetValue>(b.storage_);
    case 9: return std::get<VersionRangeValue>(a.storage_) < std::get<VersionRangeValue>(b.storage_);
    case 10: return std::get<ArchitectureSetValue>(a.storage_) < std::get<ArchitectureSetValue>(b.storage_);
    case 11: return std::get<NumericRangeValue>(a.storage_) < std::get<NumericRangeValue>(b.storage_);
    case 12: {
      const RecordValue& left = std::get<RecordValue>(a.storage_);
      const RecordValue& right = std::get<RecordValue>(b.storage_);
      const std::size_t count = std::min(left.fields.size(), right.fields.size());
      for (std::size_t i = 0; i < count; ++i) {
        if (left.fields[i].name != right.fields[i].name) return left.fields[i].name < right.fields[i].name;
        const bool left_null = left.fields[i].value == nullptr;
        const bool right_null = right.fields[i].value == nullptr;
        if (left_null != right_null) return left_null;
        if (!left_null) {
          if (*left.fields[i].value < *right.fields[i].value) return true;
          if (*right.fields[i].value < *left.fields[i].value) return false;
        }
      }
      return left.fields.size() < right.fields.size();
    }
    case 13: return std::get<RelationshipValue>(a.storage_) < std::get<RelationshipValue>(b.storage_);
    case 14: return describe_condition(std::get<PredicateValue>(a.storage_).condition.get()) <
                   describe_condition(std::get<PredicateValue>(b.storage_).condition.get());
    default: return false;
  }
}

Status validate_value(const CapabilityValue& value, const RegistryLimits& limits) {
  switch (value.kind()) {
    case ValueKind::None:
      return Status::success();
    case ValueKind::Boolean:
    case ValueKind::Bytes:
    case ValueKind::Rate:
    case ValueKind::Bandwidth:
    case ValueKind::LatencyClass:
    case ValueKind::NumericRange: {
      if (value.kind() == ValueKind::NumericRange) {
        const NumericRangeValue& range = std::get<NumericRangeValue>(value.storage());
        if (range.minimum > range.maximum) {
          return Status::failure(ErrorCode::InvalidRange, "numeric range minimum exceeds maximum");
        }
        if (range.step <= 0) {
          return Status::failure(ErrorCode::InvalidRange, "numeric range step must be positive");
        }
        if (range.maximum - range.minimum < 0) {
          return Status::failure(ErrorCode::InvalidRange, "numeric range width overflows");
        }
      }
      return Status::success();
    }
    case ValueKind::Count: {
      const CountValue& count = std::get<CountValue>(value.storage());
      if (!count.unit.empty() && !is_canonical_token(count.unit)) {
        return Status::failure(ErrorCode::InvalidValue, "count unit is not a canonical token");
      }
      return Status::success();
    }
    case ValueKind::Enumeration: {
      const EnumValue& enumeration = std::get<EnumValue>(value.storage());
      if (!is_canonical_token(enumeration.domain) || !is_canonical_token(enumeration.value)) {
        return Status::failure(ErrorCode::InvalidValue, "enumeration domain and value must be canonical tokens");
      }
      return Status::success();
    }
    case ValueKind::FeatureSet: {
      const FeatureSetValue& set = std::get<FeatureSetValue>(value.storage());
      if (set.features.size() > limits.max_feature_set_entries) {
        return Status::failure(ErrorCode::LimitExceeded, "feature set exceeds entry limit");
      }
      std::string previous;
      bool first = true;
      for (const std::string& feature : set.features) {
        if (!is_canonical_token(feature)) {
          return Status::failure(ErrorCode::InvalidValue, "feature token is not canonical");
        }
        if (!first && !(previous < feature)) {
          return Status::failure(ErrorCode::InvalidValue, "feature set must be sorted and deduplicated");
        }
        first = false;
        previous = feature;
      }
      return Status::success();
    }
    case ValueKind::VersionRange: {
      const VersionRangeValue& range = std::get<VersionRangeValue>(value.storage());
      if (!range.range.valid()) {
        return Status::failure(ErrorCode::InvalidRange, "version range minimum exceeds maximum");
      }
      if (!range.range.minimum.valid() && !range.range.maximum.valid()) {
        return Status::failure(ErrorCode::InvalidRange, "version range has no bound");
      }
      return Status::success();
    }
    case ValueKind::ArchitectureSet: {
      const ArchitectureSetValue& set = std::get<ArchitectureSetValue>(value.storage());
      if (set.architectures.size() > limits.max_architecture_set_entries) {
        return Status::failure(ErrorCode::LimitExceeded, "architecture set exceeds entry limit");
      }
      for (std::size_t i = 0; i < set.architectures.size(); ++i) {
        if (!set.architectures[i].valid()) {
          return Status::failure(ErrorCode::InvalidValue, "architecture set contains an invalid identity");
        }
        if (i != 0 && !(set.architectures[i - 1] < set.architectures[i])) {
          return Status::failure(ErrorCode::InvalidValue, "architecture set must be sorted and deduplicated");
        }
      }
      return Status::success();
    }
    case ValueKind::Record: {
      const RecordValue& record = std::get<RecordValue>(value.storage());
      return validate_record(record, limits, 1);
    }
    case ValueKind::Relationship: {
      const RelationshipValue& relationship = std::get<RelationshipValue>(value.storage());
      if (!relationship.target.valid()) {
        return Status::failure(ErrorCode::InvalidValue, "capability relationship target is invalid");
      }
      return Status::success();
    }
    case ValueKind::Predicate: {
      const PredicateValue& predicate = std::get<PredicateValue>(value.storage());
      if (predicate.condition == nullptr) {
        return Status::failure(ErrorCode::InvalidCondition, "predicate value without condition");
      }
      return validate_condition(*predicate.condition, limits);
    }
  }
  return Status::success();
}

Status normalize_value(CapabilityValue& value, const RegistryLimits& limits) {
  switch (value.kind()) {
    case ValueKind::FeatureSet: {
      FeatureSetValue& set = std::get<FeatureSetValue>(value.mutable_storage());
      std::sort(set.features.begin(), set.features.end());
      set.features.erase(std::unique(set.features.begin(), set.features.end()), set.features.end());
      break;
    }
    case ValueKind::ArchitectureSet: {
      ArchitectureSetValue& set = std::get<ArchitectureSetValue>(value.mutable_storage());
      std::sort(set.architectures.begin(), set.architectures.end());
      set.architectures.erase(std::unique(set.architectures.begin(), set.architectures.end()),
                              set.architectures.end());
      break;
    }
    case ValueKind::Record: {
      RecordValue& record = std::get<RecordValue>(value.mutable_storage());
      for (RecordField& field : record.fields) {
        if (field.value != nullptr) {
          CapabilityValue copy = *field.value;
          const Status status = normalize_value(copy, limits);
          if (!status.ok()) return status;
          field.value = std::make_shared<const CapabilityValue>(std::move(copy));
        }
      }
      std::sort(record.fields.begin(), record.fields.end(),
                [](const RecordField& a, const RecordField& b) { return a.name < b.name; });
      for (std::size_t i = 1; i < record.fields.size(); ++i) {
        if (record.fields[i - 1].name == record.fields[i].name) {
          return Status::failure(ErrorCode::InvalidValue, "record contains duplicate field names");
        }
      }
      break;
    }
    default:
      break;
  }
  return validate_value(value, limits);
}

}  // namespace hcr
