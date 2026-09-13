// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/detail/codec.hpp"

#include <algorithm>
#include <memory>

namespace hcr::detail {
namespace {

template <typename Tag>
void write_id(ByteWriter& writer, const Id<Tag>& id) { writer.str(id.str(), kMaxTokenLength); }

template <typename Tag>
Id<Tag> read_id(ByteReader& reader) { return Id<Tag>::parse(reader.str(kMaxTokenLength)); }

template <typename Tag>
void write_gen(ByteWriter& writer, const Gen<Tag>& value) { writer.u64(value.value()); }

template <typename Tag>
Gen<Tag> read_gen(ByteReader& reader) { return Gen<Tag>{reader.u64()}; }

void write_version(ByteWriter& writer, const SoftwareVersion& version, const RegistryLimits& limits) {
  writer.str(version.raw(), limits.max_name_length);
}

SoftwareVersion read_version(ByteReader& reader, const RegistryLimits& limits) {
  return SoftwareVersion::parse(reader.str(limits.max_name_length));
}

void write_version_range(ByteWriter& writer, const VersionRange& range, const RegistryLimits& limits) {
  write_version(writer, range.minimum, limits);
  write_version(writer, range.maximum, limits);
  writer.boolean(range.include_maximum);
}

VersionRange read_version_range(ByteReader& reader, const RegistryLimits& limits) {
  VersionRange range;
  range.minimum = read_version(reader, limits);
  range.maximum = read_version(reader, limits);
  range.include_maximum = reader.boolean();
  return range;
}

void write_u64_list(ByteWriter& writer, const std::vector<std::string>& values, const RegistryLimits& limits,
                    std::size_t bound) {
  const std::size_t count = std::min(values.size(), bound);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) writer.str(values[i], limits.max_metadata_bytes);
}

template <typename Tag>
std::vector<Id<Tag>> read_id_list(ByteReader& reader, std::size_t bound) {
  std::vector<Id<Tag>> out;
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return out;
  if (count > bound || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "identity list count exceeds the bound");
    return out;
  }
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) out.push_back(read_id<Tag>(reader));
  return out;
}

std::vector<std::string> read_string_list(ByteReader& reader, const RegistryLimits& limits, std::size_t bound) {
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return {};
  if (count > bound || count > reader.remaining()) {
    reader.fail(ErrorCode::CorruptData, "string list count exceeds the bound");
    return {};
  }
  std::vector<std::string> out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) out.push_back(reader.str(limits.max_metadata_bytes));
  return out;
}

}  // namespace

void write_publisher_identity(ByteWriter& writer, const PublisherIdentity& identity) {
  write_id(writer, identity.id);
  write_gen(writer, identity.boot);
  write_gen(writer, identity.epoch);
}

PublisherIdentity read_publisher_identity(ByteReader& reader) {
  PublisherIdentity identity;
  identity.id = read_id<PublisherIdTag>(reader);
  identity.boot = read_gen<PublisherBootTag>(reader);
  identity.epoch = read_gen<CoordinatorEpochTag>(reader);
  return identity;
}

void write_condition(ByteWriter& writer, const ConditionNode* node, const RegistryLimits& limits) {
  if (node == nullptr) {
    writer.u8(0u);
    return;
  }
  writer.u8(1u);
  writer.u16(static_cast<std::uint16_t>(node->kind));
  writer.str(node->key, limits.max_name_length);
  writer.str(node->value, limits.max_name_length);
  const std::size_t children = std::min(node->children.size(), limits.max_condition_children);
  writer.u32(static_cast<std::uint32_t>(children));
  for (std::size_t i = 0; i < children; ++i) write_condition(writer, node->children[i].get(), limits);
}

ConditionPtr read_condition(ByteReader& reader, const RegistryLimits& limits, std::size_t depth) {
  const std::uint8_t present = reader.u8();
  if (!reader.ok()) return nullptr;
  if (present == 0u) return nullptr;
  if (present != 1u) {
    reader.fail(ErrorCode::CorruptData, "invalid condition presence marker");
    return nullptr;
  }
  if (depth > limits.max_record_depth + 2u) {
    reader.fail(ErrorCode::LimitExceeded, "condition nesting exceeds the bound");
    return nullptr;
  }
  const std::uint16_t raw_kind = reader.u16();
  auto node = std::make_shared<ConditionNode>();
  if (raw_kind > static_cast<std::uint16_t>(ConditionKind::Not)) {
    reader.fail(ErrorCode::InvalidCondition, "unknown condition kind on the wire");
    return nullptr;
  }
  node->kind = static_cast<ConditionKind>(raw_kind);
  node->key = reader.str(limits.max_name_length);
  node->value = reader.str(limits.max_name_length);
  const std::uint32_t children = reader.u32();
  if (!reader.ok()) return nullptr;
  if (children > limits.max_condition_children || children > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "condition child count exceeds the bound");
    return nullptr;
  }
  for (std::uint32_t i = 0; i < children; ++i) {
    node->children.push_back(read_condition(reader, limits, depth + 1));
    if (!reader.ok()) return nullptr;
  }
  return node;
}

void write_value(ByteWriter& writer, const CapabilityValue& value, const RegistryLimits& limits) {
  writer.u16(static_cast<std::uint16_t>(value.kind()));
  const auto& storage = value.storage();
  switch (value.kind()) {
    case ValueKind::None:
      break;
    case ValueKind::Boolean:
      writer.boolean(std::get<bool>(storage));
      break;
    case ValueKind::Count: {
      const CountValue& count = std::get<CountValue>(storage);
      writer.u64(count.count);
      writer.str(count.unit, limits.max_name_length);
      break;
    }
    case ValueKind::Bytes:
      writer.u64(std::get<BytesValue>(storage).bytes);
      break;
    case ValueKind::Rate: {
      const RateValue& rate = std::get<RateValue>(storage);
      writer.u64(rate.magnitude);
      writer.u16(static_cast<std::uint16_t>(rate.unit));
      break;
    }
    case ValueKind::Bandwidth: {
      const BandwidthValue& bandwidth = std::get<BandwidthValue>(storage);
      writer.u64(bandwidth.magnitude);
      writer.u16(static_cast<std::uint16_t>(bandwidth.unit));
      break;
    }
    case ValueKind::LatencyClass:
      writer.u16(static_cast<std::uint16_t>(std::get<LatencyClass>(storage)));
      break;
    case ValueKind::Enumeration: {
      const EnumValue& enumeration = std::get<EnumValue>(storage);
      writer.str(enumeration.domain, limits.max_name_length);
      writer.str(enumeration.value, limits.max_name_length);
      break;
    }
    case ValueKind::FeatureSet:
      write_u64_list(writer, std::get<FeatureSetValue>(storage).features, limits, limits.max_feature_set_entries);
      break;
    case ValueKind::VersionRange:
      write_version_range(writer, std::get<VersionRangeValue>(storage).range, limits);
      break;
    case ValueKind::ArchitectureSet: {
      const ArchitectureSetValue& set = std::get<ArchitectureSetValue>(storage);
      const std::size_t count = std::min(set.architectures.size(), limits.max_architecture_set_entries);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) write_id(writer, set.architectures[i]);
      break;
    }
    case ValueKind::NumericRange: {
      const NumericRangeValue& range = std::get<NumericRangeValue>(storage);
      writer.i64(range.minimum);
      writer.i64(range.maximum);
      writer.i64(range.step);
      break;
    }
    case ValueKind::Record: {
      const RecordValue& record = std::get<RecordValue>(storage);
      const std::size_t count = std::min(record.fields.size(), limits.max_record_fields);
      writer.u32(static_cast<std::uint32_t>(count));
      for (std::size_t i = 0; i < count; ++i) {
        writer.str(record.fields[i].name, limits.max_name_length);
        if (record.fields[i].value == nullptr) {
          writer.u8(0u);
        } else {
          writer.u8(1u);
          write_value(writer, *record.fields[i].value, limits);
        }
      }
      break;
    }
    case ValueKind::Relationship: {
      const RelationshipValue& relationship = std::get<RelationshipValue>(storage);
      writer.u16(static_cast<std::uint16_t>(relationship.relation));
      write_id(writer, relationship.target);
      break;
    }
    case ValueKind::Predicate:
      write_condition(writer, std::get<PredicateValue>(storage).condition.get(), limits);
      break;
  }
}

CapabilityValue read_value(ByteReader& reader, const RegistryLimits& limits, std::size_t depth) {
  const std::uint16_t raw_kind = reader.u16();
  if (!reader.ok()) return {};
  if (raw_kind > static_cast<std::uint16_t>(ValueKind::Predicate)) {
    reader.fail(ErrorCode::InvalidValue, "unknown value kind on the wire");
    return {};
  }
  if (depth > limits.max_record_depth) {
    reader.fail(ErrorCode::LimitExceeded, "value nesting exceeds the bound");
    return {};
  }
  switch (static_cast<ValueKind>(raw_kind)) {
    case ValueKind::None:
      return CapabilityValue{};
    case ValueKind::Boolean:
      return CapabilityValue(reader.boolean());
    case ValueKind::Count: {
      CountValue count;
      count.count = reader.u64();
      count.unit = reader.str(limits.max_name_length);
      return CapabilityValue(count);
    }
    case ValueKind::Bytes: {
      BytesValue bytes;
      bytes.bytes = reader.u64();
      return CapabilityValue(bytes);
    }
    case ValueKind::Rate: {
      RateValue rate;
      rate.magnitude = reader.u64();
      const std::uint16_t unit = reader.u16();
      if (unit > static_cast<std::uint16_t>(RateUnit::OperationsPerSecond)) {
        reader.fail(ErrorCode::InvalidValue, "unknown rate unit on the wire");
        return {};
      }
      rate.unit = static_cast<RateUnit>(unit);
      return CapabilityValue(rate);
    }
    case ValueKind::Bandwidth: {
      BandwidthValue bandwidth;
      bandwidth.magnitude = reader.u64();
      const std::uint16_t unit = reader.u16();
      if (unit > static_cast<std::uint16_t>(kMaxBandwidthUnit)) {
        reader.fail(ErrorCode::InvalidValue, "unknown bandwidth unit on the wire");
        return {};
      }
      bandwidth.unit = static_cast<BandwidthUnit>(unit);
      return CapabilityValue(bandwidth);
    }
    case ValueKind::LatencyClass: {
      const std::uint16_t raw = reader.u16();
      if (raw > static_cast<std::uint16_t>(LatencyClass::VeryHigh)) {
        reader.fail(ErrorCode::InvalidValue, "unknown latency class on the wire");
        return {};
      }
      return CapabilityValue(static_cast<LatencyClass>(raw));
    }
    case ValueKind::Enumeration: {
      EnumValue enumeration;
      enumeration.domain = reader.str(limits.max_name_length);
      enumeration.value = reader.str(limits.max_name_length);
      return CapabilityValue(enumeration);
    }
    case ValueKind::FeatureSet: {
      FeatureSetValue set;
      set.features = read_string_list(reader, limits, limits.max_feature_set_entries);
      return CapabilityValue(set);
    }
    case ValueKind::VersionRange: {
      VersionRangeValue range;
      range.range = read_version_range(reader, limits);
      return CapabilityValue(range);
    }
    case ValueKind::ArchitectureSet: {
      ArchitectureSetValue set;
      const std::uint32_t count = reader.u32();
      if (!reader.ok()) return {};
      if (count > limits.max_architecture_set_entries || count > reader.remaining()) {
        reader.fail(ErrorCode::LimitExceeded, "architecture set count exceeds the bound");
        return {};
      }
      for (std::uint32_t i = 0; i < count; ++i) set.architectures.push_back(read_id<ArchitectureIdTag>(reader));
      return CapabilityValue(set);
    }
    case ValueKind::NumericRange: {
      NumericRangeValue range;
      range.minimum = reader.i64();
      range.maximum = reader.i64();
      range.step = reader.i64();
      return CapabilityValue(range);
    }
    case ValueKind::Record: {
      RecordValue record;
      const std::uint32_t count = reader.u32();
      if (!reader.ok()) return {};
      if (count > limits.max_record_fields || count > reader.remaining()) {
        reader.fail(ErrorCode::LimitExceeded, "record field count exceeds the bound");
        return {};
      }
      for (std::uint32_t i = 0; i < count; ++i) {
        RecordField field;
        field.name = reader.str(limits.max_name_length);
        const std::uint8_t present = reader.u8();
        if (!reader.ok()) return {};
        if (present == 1u) {
          CapabilityValue nested = read_value(reader, limits, depth + 1);
          if (!reader.ok()) return {};
          field.value = std::make_shared<const CapabilityValue>(std::move(nested));
        } else if (present != 0u) {
          reader.fail(ErrorCode::CorruptData, "invalid record field presence marker");
          return {};
        }
        record.fields.push_back(std::move(field));
      }
      return CapabilityValue(record);
    }
    case ValueKind::Relationship: {
      RelationshipValue relationship;
      const std::uint16_t raw = reader.u16();
      if (raw > static_cast<std::uint16_t>(CapabilityRelation::Exclusive)) {
        reader.fail(ErrorCode::InvalidValue, "unknown capability relation on the wire");
        return {};
      }
      relationship.relation = static_cast<CapabilityRelation>(raw);
      relationship.target = read_id<CapabilityIdTag>(reader);
      return CapabilityValue(relationship);
    }
    case ValueKind::Predicate: {
      PredicateValue predicate;
      predicate.condition = read_condition(reader, limits, depth + 1);
      return CapabilityValue(predicate);
    }
  }
  return CapabilityValue{};
}

void write_environment(ByteWriter& writer, const EnvironmentContext& environment, const RegistryLimits& limits) {
  const std::size_t count = std::min(environment.facts().size(), limits.max_query_results);
  writer.u32(static_cast<std::uint32_t>(count));
  std::size_t written = 0;
  for (const auto& entry : environment.facts()) {
    if (written++ >= count) break;
    writer.str(entry.first, limits.max_name_length);
    writer.str(entry.second, limits.max_metadata_bytes);
  }
}

EnvironmentContext read_environment(ByteReader& reader, const RegistryLimits& limits) {
  EnvironmentContext environment;
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return environment;
  if (count > limits.max_query_results || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "environment fact count exceeds the bound");
    return environment;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::string key = reader.str(limits.max_name_length);
    const std::string value = reader.str(limits.max_metadata_bytes);
    environment.set(key, value);
  }
  return environment;
}

void write_evidence_source(ByteWriter& writer, const EvidenceSource& source, const RegistryLimits& limits) {
  writer.u16(static_cast<std::uint16_t>(source.source_class));
  writer.u16(static_cast<std::uint16_t>(source.provenance));
  write_publisher_identity(writer, source.publisher);
  writer.str(source.adapter, limits.max_metadata_bytes);
  writer.str(source.native_reference, limits.max_metadata_bytes);
  writer.u64(source.observation_sequence);
  writer.str(source.observed_at_utc, limits.max_metadata_bytes);
  write_gen(writer, source.evidence_generation);
  writer.u64(source.platform_generation);
}

EvidenceSource read_evidence_source(ByteReader& reader, const RegistryLimits& limits) {
  EvidenceSource source;
  const std::uint16_t raw_source = reader.u16();
  const std::uint16_t raw_provenance = reader.u16();
  if (raw_source > static_cast<std::uint16_t>(SourceClass::HardwareProbe) ||
      raw_provenance > static_cast<std::uint16_t>(ProvenanceClass::UnknownSource)) {
    reader.fail(ErrorCode::InvalidProvenance, "unknown provenance enumeration on the wire");
    return source;
  }
  source.source_class = static_cast<SourceClass>(raw_source);
  source.provenance = static_cast<ProvenanceClass>(raw_provenance);
  source.publisher = read_publisher_identity(reader);
  source.adapter = reader.str(limits.max_metadata_bytes);
  source.native_reference = reader.str(limits.max_metadata_bytes);
  source.observation_sequence = reader.u64();
  source.observed_at_utc = reader.str(limits.max_metadata_bytes);
  source.evidence_generation = read_gen<EvidenceGenerationTag>(reader);
  source.platform_generation = reader.u64();
  return source;
}

void write_hardware_identity(ByteWriter& writer, const HardwareIdentity& identity, const RegistryLimits& limits) {
  write_id(writer, identity.vendor);
  write_id(writer, identity.product);
  write_id(writer, identity.family);
  write_id(writer, identity.model);
  write_id(writer, identity.revision);
  write_id(writer, identity.architecture);
  writer.u16(static_cast<std::uint16_t>(identity.hardware_class));
  const std::size_t count = std::min(identity.native_identifiers.size(), limits.max_native_identifiers);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    writer.str(identity.native_identifiers[i].key, limits.max_name_length);
    writer.str(identity.native_identifiers[i].value, limits.max_metadata_bytes);
  }
}

HardwareIdentity read_hardware_identity(ByteReader& reader, const RegistryLimits& limits) {
  HardwareIdentity identity;
  identity.vendor = read_id<VendorIdTag>(reader);
  identity.product = read_id<ProductIdTag>(reader);
  identity.family = read_id<HardwareFamilyIdTag>(reader);
  identity.model = read_id<HardwareModelIdTag>(reader);
  identity.revision = read_id<HardwareRevisionIdTag>(reader);
  identity.architecture = read_id<ArchitectureIdTag>(reader);
  const std::uint16_t raw_class = reader.u16();
  if (raw_class > static_cast<std::uint16_t>(HardwareClass::Other)) {
    reader.fail(ErrorCode::InvalidIdentity, "unknown hardware class on the wire");
    return identity;
  }
  identity.hardware_class = static_cast<HardwareClass>(raw_class);
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return identity;
  if (count > limits.max_native_identifiers || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "native identifier count exceeds the bound");
    return identity;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    NativeIdentifier identifier;
    identifier.key = reader.str(limits.max_name_length);
    identifier.value = reader.str(limits.max_metadata_bytes);
    identity.native_identifiers.push_back(std::move(identifier));
  }
  return identity;
}

void write_platform_identity(ByteWriter& writer, const PlatformIdentity& identity, const RegistryLimits& limits) {
  write_id(writer, identity.id);
  writer.str(identity.display_name, limits.max_name_length);
  writer.str(identity.vendor, limits.max_name_length);
  writer.str(identity.model, limits.max_name_length);
  write_id(writer, identity.architecture);
  const std::size_t count = std::min(identity.native_identifiers.size(), limits.max_native_identifiers);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    writer.str(identity.native_identifiers[i].key, limits.max_name_length);
    writer.str(identity.native_identifiers[i].value, limits.max_metadata_bytes);
  }
}

PlatformIdentity read_platform_identity(ByteReader& reader, const RegistryLimits& limits) {
  PlatformIdentity identity;
  identity.id = read_id<PlatformIdTag>(reader);
  identity.display_name = reader.str(limits.max_name_length);
  identity.vendor = reader.str(limits.max_name_length);
  identity.model = reader.str(limits.max_name_length);
  identity.architecture = read_id<ArchitectureIdTag>(reader);
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return identity;
  if (count > limits.max_native_identifiers || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "platform native identifier count exceeds the bound");
    return identity;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    NativeIdentifier identifier;
    identifier.key = reader.str(limits.max_name_length);
    identifier.value = reader.str(limits.max_metadata_bytes);
    identity.native_identifiers.push_back(std::move(identifier));
  }
  return identity;
}

void write_device_identity(ByteWriter& writer, const DeviceIdentity& identity, const RegistryLimits& limits) {
  write_id(writer, identity.id);
  write_id(writer, identity.incarnation);
  write_hardware_identity(writer, identity.hardware, limits);
  write_id(writer, identity.platform);
  writer.boolean(identity.serial_number.has_value());
  if (identity.serial_number.has_value()) writer.str(*identity.serial_number, limits.max_metadata_bytes);
  writer.boolean(identity.instance_locator.has_value());
  if (identity.instance_locator.has_value()) writer.str(*identity.instance_locator, limits.max_metadata_bytes);
  writer.str(identity.display_name, limits.max_name_length);
}

DeviceIdentity read_device_identity(ByteReader& reader, const RegistryLimits& limits) {
  DeviceIdentity identity;
  identity.id = read_id<DeviceIdTag>(reader);
  identity.incarnation = read_id<DeviceIncarnationIdTag>(reader);
  identity.hardware = read_hardware_identity(reader, limits);
  identity.platform = read_id<PlatformIdTag>(reader);
  if (reader.boolean()) identity.serial_number = reader.str(limits.max_metadata_bytes);
  if (reader.boolean()) identity.instance_locator = reader.str(limits.max_metadata_bytes);
  identity.display_name = reader.str(limits.max_name_length);
  return identity;
}

void write_software_state(ByteWriter& writer, const DeviceSoftwareState& state, const RegistryLimits& limits) {
  write_id(writer, state.firmware.id);
  writer.str(state.firmware.version, limits.max_name_length);
  writer.str(state.firmware.build, limits.max_name_length);
  write_gen(writer, state.firmware_generation);
  write_id(writer, state.driver.id);
  writer.str(state.driver.version, limits.max_name_length);
  write_gen(writer, state.driver_generation);
  write_id(writer, state.runtime.id);
  writer.str(state.runtime.version, limits.max_name_length);
  write_gen(writer, state.runtime_generation);
}

DeviceSoftwareState read_software_state(ByteReader& reader, const RegistryLimits& limits) {
  DeviceSoftwareState state;
  state.firmware.id = read_id<FirmwareIdTag>(reader);
  state.firmware.version = reader.str(limits.max_name_length);
  state.firmware.build = reader.str(limits.max_name_length);
  state.firmware_generation = read_gen<FirmwareGenerationTag>(reader);
  state.driver.id = read_id<DriverIdTag>(reader);
  state.driver.version = reader.str(limits.max_name_length);
  state.driver_generation = read_gen<DriverGenerationTag>(reader);
  state.runtime.id = read_id<RuntimeIdTag>(reader);
  state.runtime.version = reader.str(limits.max_name_length);
  state.runtime_generation = read_gen<RuntimeGenerationTag>(reader);
  return state;
}

void write_device_registration(ByteWriter& writer, const DeviceRegistration& registration, const RegistryLimits& limits) {
  write_device_identity(writer, registration.identity, limits);
  write_software_state(writer, registration.software, limits);
  write_platform_identity(writer, registration.platform, limits);
  writer.boolean(registration.device_boot_observed);
  writer.u64(registration.observation_sequence);
  writer.str(registration.observed_at_utc, limits.max_metadata_bytes);
}

DeviceRegistration read_device_registration(ByteReader& reader, const RegistryLimits& limits) {
  DeviceRegistration registration;
  registration.identity = read_device_identity(reader, limits);
  registration.software = read_software_state(reader, limits);
  registration.platform = read_platform_identity(reader, limits);
  registration.device_boot_observed = reader.boolean();
  registration.observation_sequence = reader.u64();
  registration.observed_at_utc = reader.str(limits.max_metadata_bytes);
  return registration;
}

void write_capability_schema(ByteWriter& writer, const CapabilitySchema& schema, const RegistryLimits& limits) {
  write_id(writer, schema.id);
  write_id(writer, schema.capability);
  writer.u16(static_cast<std::uint16_t>(schema.domain));
  writer.u16(static_cast<std::uint16_t>(schema.value_kind));
  writer.str(schema.canonical_name, limits.max_name_length);
  writer.str(schema.unit, limits.max_metadata_bytes);
}

CapabilitySchema read_capability_schema(ByteReader& reader, const RegistryLimits& limits) {
  CapabilitySchema schema;
  schema.id = read_id<CapabilitySchemaIdTag>(reader);
  schema.capability = read_id<CapabilityIdTag>(reader);
  const std::uint16_t domain = reader.u16();
  const std::uint16_t kind = reader.u16();
  if (domain > static_cast<std::uint16_t>(CapabilityDomain::Platform) ||
      kind > static_cast<std::uint16_t>(ValueKind::Predicate)) {
    reader.fail(ErrorCode::InvalidCapability, "unknown capability domain or value kind on the wire");
    return schema;
  }
  schema.domain = static_cast<CapabilityDomain>(domain);
  schema.value_kind = static_cast<ValueKind>(kind);
  schema.canonical_name = reader.str(limits.max_name_length);
  schema.unit = reader.str(limits.max_metadata_bytes);
  return schema;
}

void write_capability_record(ByteWriter& writer, const CapabilityRecord& record, const RegistryLimits& limits) {
  write_id(writer, record.device);
  write_gen(writer, record.device_generation);
  write_gen(writer, record.device_boot);
  write_id(writer, record.capability);
  write_id(writer, record.schema);
  write_gen(writer, record.capability_generation);
  writer.u16(static_cast<std::uint16_t>(record.support));
  write_value(writer, record.value, limits);
  writer.u16(static_cast<std::uint16_t>(record.precision));
  write_evidence_source(writer, record.source, limits);
  write_condition(writer, record.conditions.get(), limits);
  write_gen(writer, record.firmware_generation);
  write_gen(writer, record.driver_generation);
  write_gen(writer, record.runtime_generation);
  write_gen(writer, record.platform_generation);
  write_id(writer, record.driver_id);
  write_id(writer, record.firmware_id);
  write_id(writer, record.runtime_id);
  writer.boolean(record.withdrawn);
}

CapabilityRecord read_capability_record(ByteReader& reader, const RegistryLimits& limits) {
  CapabilityRecord record;
  record.device = read_id<DeviceIdTag>(reader);
  record.device_generation = read_gen<DeviceGenerationTag>(reader);
  record.device_boot = read_gen<DeviceBootTag>(reader);
  record.capability = read_id<CapabilityIdTag>(reader);
  record.schema = read_id<CapabilitySchemaIdTag>(reader);
  record.capability_generation = read_gen<CapabilityGenerationTag>(reader);
  const std::uint16_t support = reader.u16();
  if (support > static_cast<std::uint16_t>(SupportState::RevalidationRequired)) {
    reader.fail(ErrorCode::InvalidSupportState, "unknown support state on the wire");
    return record;
  }
  record.support = static_cast<SupportState>(support);
  record.value = read_value(reader, limits, 1);
  const std::uint16_t precision = reader.u16();
  if (precision > static_cast<std::uint16_t>(PrecisionClass::Unknown)) {
    reader.fail(ErrorCode::InvalidProvenance, "unknown precision class on the wire");
    return record;
  }
  record.precision = static_cast<PrecisionClass>(precision);
  record.source = read_evidence_source(reader, limits);
  record.conditions = read_condition(reader, limits, 1);
  record.firmware_generation = read_gen<FirmwareGenerationTag>(reader);
  record.driver_generation = read_gen<DriverGenerationTag>(reader);
  record.runtime_generation = read_gen<RuntimeGenerationTag>(reader);
  record.platform_generation = read_gen<PlatformGenerationTag>(reader);
  record.driver_id = read_id<DriverIdTag>(reader);
  record.firmware_id = read_id<FirmwareIdTag>(reader);
  record.runtime_id = read_id<RuntimeIdTag>(reader);
  record.withdrawn = reader.boolean();
  return record;
}

void write_quirk_record(ByteWriter& writer, const QuirkRecord& quirk, const RegistryLimits& limits) {
  write_id(writer, quirk.id);
  write_gen(writer, quirk.quirk_generation);
  writer.str(quirk.title, limits.max_name_length);
  writer.str(quirk.description, limits.max_metadata_bytes);
  writer.u16(static_cast<std::uint16_t>(quirk.category));
  writer.u16(static_cast<std::uint16_t>(quirk.severity));
  writer.u16(static_cast<std::uint16_t>(quirk.mitigation));
  const auto write_ids = [&writer, &limits](const auto& values) {
    const std::size_t count = std::min(values.size(), static_cast<std::size_t>(1024));
    writer.u32(static_cast<std::uint32_t>(count));
    for (std::size_t i = 0; i < count; ++i) write_id(writer, values[i]);
  };
  write_ids(quirk.applicability.vendors);
  write_ids(quirk.applicability.families);
  write_ids(quirk.applicability.models);
  write_ids(quirk.applicability.devices);
  write_ids(quirk.applicability.revisions);
  write_ids(quirk.applicability.architectures);
  writer.u32(static_cast<std::uint32_t>(
      std::min(quirk.applicability.revision_ranges.size(), static_cast<std::size_t>(64))));
  for (std::size_t i = 0; i < quirk.applicability.revision_ranges.size() && i < 64; ++i) {
    writer.str(quirk.applicability.revision_ranges[i].minimum, limits.max_metadata_bytes);
    writer.str(quirk.applicability.revision_ranges[i].maximum, limits.max_metadata_bytes);
  }
  const auto write_ranges = [&writer, &limits](const std::vector<VersionRange>& ranges) {
    writer.u32(static_cast<std::uint32_t>(std::min(ranges.size(), static_cast<std::size_t>(64))));
    for (std::size_t i = 0; i < ranges.size() && i < 64; ++i) write_version_range(writer, ranges[i], limits);
  };
  write_ranges(quirk.applicability.driver_ranges);
  write_ranges(quirk.applicability.firmware_ranges);
  write_ranges(quirk.applicability.runtime_ranges);
  write_condition(writer, quirk.applicability.platform_conditions.get(), limits);
  write_ids(quirk.impacted_capabilities);
  write_evidence_source(writer, quirk.source, limits);
  writer.boolean(quirk.withdrawn);
}

QuirkRecord read_quirk_record(ByteReader& reader, const RegistryLimits& limits) {
  QuirkRecord quirk;
  quirk.id = read_id<QuirkIdTag>(reader);
  quirk.quirk_generation = read_gen<QuirkGenerationTag>(reader);
  quirk.title = reader.str(limits.max_name_length);
  quirk.description = reader.str(limits.max_metadata_bytes);
  const std::uint16_t category = reader.u16();
  const std::uint16_t severity = reader.u16();
  const std::uint16_t mitigation = reader.u16();
  if (category > static_cast<std::uint16_t>(QuirkCategory::Other) ||
      severity > static_cast<std::uint16_t>(QuirkSeverity::Fatal) ||
      mitigation > static_cast<std::uint16_t>(MitigationKind::ContactVendor)) {
    reader.fail(ErrorCode::CorruptData, "unknown quirk enumeration on the wire");
    return quirk;
  }
  quirk.category = static_cast<QuirkCategory>(category);
  quirk.severity = static_cast<QuirkSeverity>(severity);
  quirk.mitigation = static_cast<MitigationKind>(mitigation);
  quirk.applicability.vendors = read_id_list<VendorIdTag>(reader, 1024);
  quirk.applicability.families = read_id_list<HardwareFamilyIdTag>(reader, 1024);
  quirk.applicability.models = read_id_list<HardwareModelIdTag>(reader, 1024);
  quirk.applicability.devices = read_id_list<DeviceIdTag>(reader, 1024);
  quirk.applicability.revisions = read_id_list<HardwareRevisionIdTag>(reader, 1024);
  quirk.applicability.architectures = read_id_list<ArchitectureIdTag>(reader, 1024);
  {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) return quirk;
    if (count > 64 || count > reader.remaining()) {
      reader.fail(ErrorCode::LimitExceeded, "revision range count exceeds the bound");
      return quirk;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      RevisionRange range;
      range.minimum = reader.str(limits.max_metadata_bytes);
      range.maximum = reader.str(limits.max_metadata_bytes);
      quirk.applicability.revision_ranges.push_back(std::move(range));
    }
  }
  const auto read_ranges = [&reader, &limits](std::vector<VersionRange>& ranges) {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) return;
    if (count > 64 || count > reader.remaining()) {
      reader.fail(ErrorCode::LimitExceeded, "version range count exceeds the bound");
      return;
    }
    for (std::uint32_t i = 0; i < count; ++i) ranges.push_back(read_version_range(reader, limits));
  };
  read_ranges(quirk.applicability.driver_ranges);
  read_ranges(quirk.applicability.firmware_ranges);
  read_ranges(quirk.applicability.runtime_ranges);
  quirk.applicability.platform_conditions = read_condition(reader, limits, 1);
  quirk.impacted_capabilities = read_id_list<CapabilityIdTag>(reader, 1024);
  quirk.source = read_evidence_source(reader, limits);
  quirk.withdrawn = reader.boolean();
  return quirk;
}

void write_compatibility_fact(ByteWriter& writer, const CompatibilityFact& fact, const RegistryLimits& limits) {
  write_id(writer, fact.id);
  write_gen(writer, fact.compatibility_generation);
  writer.u16(static_cast<std::uint16_t>(fact.kind));
  write_hardware_reference(writer, fact.lhs, limits);
  write_hardware_reference(writer, fact.rhs, limits);
  writer.u16(static_cast<std::uint16_t>(fact.outcome));
  write_condition(writer, fact.conditions.get(), limits);
  write_version_range(writer, fact.version_window, limits);
  writer.str(fact.reason, limits.max_metadata_bytes);
  write_evidence_source(writer, fact.source, limits);
  writer.boolean(fact.withdrawn);
}

CompatibilityFact read_compatibility_fact(ByteReader& reader, const RegistryLimits& limits) {
  CompatibilityFact fact;
  fact.id = read_id<CompatibilityFactIdTag>(reader);
  fact.compatibility_generation = read_gen<CompatibilityGenerationTag>(reader);
  const std::uint16_t kind = reader.u16();
  if (kind > static_cast<std::uint16_t>(CompatibilitySubjectKind::Other)) {
    reader.fail(ErrorCode::CorruptData, "unknown compatibility subject kind on the wire");
    return fact;
  }
  fact.kind = static_cast<CompatibilitySubjectKind>(kind);
  fact.lhs = read_hardware_reference(reader, limits);
  fact.rhs = read_hardware_reference(reader, limits);
  const std::uint16_t outcome = reader.u16();
  if (outcome > static_cast<std::uint16_t>(CompatibilityOutcome::RevalidationRequired)) {
    reader.fail(ErrorCode::CorruptData, "unknown compatibility outcome on the wire");
    return fact;
  }
  fact.outcome = static_cast<CompatibilityOutcome>(outcome);
  fact.conditions = read_condition(reader, limits, 1);
  fact.version_window = read_version_range(reader, limits);
  fact.reason = reader.str(limits.max_metadata_bytes);
  fact.source = read_evidence_source(reader, limits);
  fact.withdrawn = reader.boolean();
  return fact;
}

void write_hardware_reference(ByteWriter& writer, const HardwareReference& reference, const RegistryLimits& limits) {
  writer.u16(static_cast<std::uint16_t>(reference.kind));
  writer.str(reference.token, kMaxTokenLength);
  write_version(writer, reference.version, limits);
}

HardwareReference read_hardware_reference(ByteReader& reader, const RegistryLimits& limits) {
  HardwareReference reference;
  const std::uint16_t kind = reader.u16();
  if (kind > static_cast<std::uint16_t>(HardwareReference::Kind::Platform)) {
    reader.fail(ErrorCode::InvalidReference, "unknown hardware reference kind on the wire");
    return reference;
  }
  reference.kind = static_cast<HardwareReference::Kind>(kind);
  reference.token = reader.str(kMaxTokenLength);
  reference.version = read_version(reader, limits);
  return reference;
}

void write_subject_snapshot(ByteWriter& writer, const SubjectSnapshot& subject, const RegistryLimits& limits) {
  write_device_identity(writer, subject.identity, limits);
  write_software_state(writer, subject.software, limits);
  write_gen(writer, subject.generation);
  write_gen(writer, subject.boot);
  write_gen(writer, subject.platform_generation);
  writer.u16(static_cast<std::uint16_t>(subject.classification));
  writer.u64(subject.capability_count);
  writer.u64(subject.current_count);
  writer.u64(subject.unknown_count);
  writer.u64(subject.unsupported_count);
  writer.u64(subject.stale_count);
  writer.u64(subject.applicable_quirk_count);
}

SubjectSnapshot read_subject_snapshot(ByteReader& reader, const RegistryLimits& limits) {
  SubjectSnapshot subject;
  subject.identity = read_device_identity(reader, limits);
  subject.software = read_software_state(reader, limits);
  subject.generation = read_gen<DeviceGenerationTag>(reader);
  subject.boot = read_gen<DeviceBootTag>(reader);
  subject.platform_generation = read_gen<PlatformGenerationTag>(reader);
  const std::uint16_t classification = reader.u16();
  if (classification > static_cast<std::uint16_t>(TruthClass::Unknown)) {
    reader.fail(ErrorCode::CorruptData, "unknown truth classification on the wire");
    return subject;
  }
  subject.classification = static_cast<TruthClass>(classification);
  subject.capability_count = static_cast<std::size_t>(reader.u64());
  subject.current_count = static_cast<std::size_t>(reader.u64());
  subject.unknown_count = static_cast<std::size_t>(reader.u64());
  subject.unsupported_count = static_cast<std::size_t>(reader.u64());
  subject.stale_count = static_cast<std::size_t>(reader.u64());
  subject.applicable_quirk_count = static_cast<std::size_t>(reader.u64());
  return subject;
}

void write_publisher_record(ByteWriter& writer, const PublisherRecord& record, const RegistryLimits& limits) {
  write_publisher_identity(writer, record.identity);
  writer.u16(static_cast<std::uint16_t>(record.state));
  writer.str(record.adapter, limits.max_metadata_bytes);
  writer.u64(record.last_sequence);
  write_gen(writer, record.max_evidence_generation);
  writer.u64(record.accepted);
  writer.u64(record.rejected);
  writer.str(record.reason, limits.max_metadata_bytes);
  writer.str(record.registered_at_utc, limits.max_metadata_bytes);
  writer.u64(record.boot_history);
}

PublisherRecord read_publisher_record(ByteReader& reader, const RegistryLimits& limits) {
  PublisherRecord record;
  record.identity = read_publisher_identity(reader);
  const std::uint16_t state = reader.u16();
  if (state > static_cast<std::uint16_t>(PublisherState::Dead)) {
    reader.fail(ErrorCode::CorruptData, "unknown publisher state on the wire");
    return record;
  }
  record.state = static_cast<PublisherState>(state);
  record.adapter = reader.str(limits.max_metadata_bytes);
  record.last_sequence = reader.u64();
  record.max_evidence_generation = read_gen<EvidenceGenerationTag>(reader);
  record.accepted = static_cast<std::size_t>(reader.u64());
  record.rejected = static_cast<std::size_t>(reader.u64());
  record.reason = reader.str(limits.max_metadata_bytes);
  record.registered_at_utc = reader.str(limits.max_metadata_bytes);
  record.boot_history = static_cast<std::size_t>(reader.u64());
  return record;
}

void write_evidence_view(ByteWriter& writer, const EvidenceView& view, const RegistryLimits& limits) {
  write_gen(writer, view.capability_generation);
  writer.u16(static_cast<std::uint16_t>(view.support));
  write_value(writer, view.value, limits);
  writer.u16(static_cast<std::uint16_t>(view.precision));
  write_evidence_source(writer, view.source, limits);
  writer.u32(static_cast<std::uint32_t>(view.staleness));
  writer.boolean(view.winner);
  writer.boolean(view.withdrawn);
}

EvidenceView read_evidence_view(ByteReader& reader, const RegistryLimits& limits) {
  EvidenceView view;
  view.capability_generation = read_gen<CapabilityGenerationTag>(reader);
  const std::uint16_t support = reader.u16();
  if (support > static_cast<std::uint16_t>(SupportState::RevalidationRequired)) {
    reader.fail(ErrorCode::InvalidSupportState, "unknown support state on the wire");
    return view;
  }
  view.support = static_cast<SupportState>(support);
  view.value = read_value(reader, limits, 1);
  const std::uint16_t precision = reader.u16();
  if (precision > static_cast<std::uint16_t>(PrecisionClass::Unknown)) {
    reader.fail(ErrorCode::InvalidProvenance, "unknown precision class on the wire");
    return view;
  }
  view.precision = static_cast<PrecisionClass>(precision);
  view.source = read_evidence_source(reader, limits);
  view.staleness = static_cast<StalenessFlag>(reader.u32());
  view.winner = reader.boolean();
  view.withdrawn = reader.boolean();
  return view;
}

void write_quirk_application(ByteWriter& writer, const QuirkApplication& application, const RegistryLimits& limits) {
  write_id(writer, application.id);
  write_gen(writer, application.quirk_generation);
  writer.u16(static_cast<std::uint16_t>(application.severity));
  writer.u16(static_cast<std::uint16_t>(application.mitigation));
  writer.u16(static_cast<std::uint16_t>(application.result));
  writer.str(application.reason, limits.max_metadata_bytes);
  writer.str(application.title, limits.max_name_length);
}

QuirkApplication read_quirk_application(ByteReader& reader, const RegistryLimits& limits) {
  QuirkApplication application;
  application.id = read_id<QuirkIdTag>(reader);
  application.quirk_generation = read_gen<QuirkGenerationTag>(reader);
  const std::uint16_t severity = reader.u16();
  const std::uint16_t mitigation = reader.u16();
  const std::uint16_t result = reader.u16();
  if (severity > static_cast<std::uint16_t>(QuirkSeverity::Fatal) ||
      mitigation > static_cast<std::uint16_t>(MitigationKind::ContactVendor) ||
      result > static_cast<std::uint16_t>(QuirkApplicabilityResult::Unknown)) {
    reader.fail(ErrorCode::CorruptData, "unknown quirk application enumeration on the wire");
    return application;
  }
  application.severity = static_cast<QuirkSeverity>(severity);
  application.mitigation = static_cast<MitigationKind>(mitigation);
  application.result = static_cast<QuirkApplicabilityResult>(result);
  application.reason = reader.str(limits.max_metadata_bytes);
  application.title = reader.str(limits.max_name_length);
  return application;
}

void write_condition_evaluation(ByteWriter& writer, const ConditionEvaluation& evaluation, const RegistryLimits& limits) {
  writer.u16(static_cast<std::uint16_t>(evaluation.outcome));
  const std::size_t count = std::min(evaluation.evidence.size(), limits.max_condition_nodes);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    writer.str(evaluation.evidence[i].expression, limits.max_metadata_bytes);
    writer.u16(static_cast<std::uint16_t>(evaluation.evidence[i].outcome));
    writer.str(evaluation.evidence[i].detail, limits.max_metadata_bytes);
  }
}

ConditionEvaluation read_condition_evaluation(ByteReader& reader, const RegistryLimits& limits) {
  ConditionEvaluation evaluation;
  const std::uint16_t outcome = reader.u16();
  if (outcome > static_cast<std::uint16_t>(ConditionOutcome::NotApplicable)) {
    reader.fail(ErrorCode::InvalidCondition, "unknown condition outcome on the wire");
    return evaluation;
  }
  evaluation.outcome = static_cast<ConditionOutcome>(outcome);
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return evaluation;
  if (count > limits.max_condition_nodes || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "condition evidence count exceeds the bound");
    return evaluation;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    ConditionEvidence evidence;
    evidence.expression = reader.str(limits.max_metadata_bytes);
    const std::uint16_t leaf_outcome = reader.u16();
    if (leaf_outcome > static_cast<std::uint16_t>(ConditionOutcome::NotApplicable)) {
      reader.fail(ErrorCode::InvalidCondition, "unknown leaf condition outcome on the wire");
      return evaluation;
    }
    evidence.outcome = static_cast<ConditionOutcome>(leaf_outcome);
    evidence.detail = reader.str(limits.max_metadata_bytes);
    evaluation.evidence.push_back(std::move(evidence));
  }
  return evaluation;
}

void write_resolved_capability(ByteWriter& writer, const ResolvedCapability& value, const RegistryLimits& limits) {
  write_id(writer, value.device);
  write_gen(writer, value.device_generation);
  write_gen(writer, value.device_boot);
  write_id(writer, value.capability);
  write_id(writer, value.schema);
  write_gen(writer, value.capability_generation);
  writer.u16(static_cast<std::uint16_t>(value.declared_support));
  writer.u16(static_cast<std::uint16_t>(value.effective_support));
  write_value(writer, value.value, limits);
  writer.u16(static_cast<std::uint16_t>(value.precision));
  writer.u16(static_cast<std::uint16_t>(value.truth));
  writer.u16(static_cast<std::uint16_t>(value.contradiction));
  writer.u32(static_cast<std::uint32_t>(value.staleness));
  write_condition(writer, value.conditions.get(), limits);
  write_condition_evaluation(writer, value.condition_evaluation, limits);
  const std::size_t quirks = std::min(value.quirks.size(), limits.max_query_results);
  writer.u32(static_cast<std::uint32_t>(quirks));
  for (std::size_t i = 0; i < quirks; ++i) write_quirk_application(writer, value.quirks[i], limits);
  const std::size_t evidence = std::min(value.evidence.size(), limits.max_query_results);
  writer.u32(static_cast<std::uint32_t>(evidence));
  for (std::size_t i = 0; i < evidence; ++i) write_evidence_view(writer, value.evidence[i], limits);
  write_gen(writer, value.firmware_generation);
  write_gen(writer, value.driver_generation);
  write_gen(writer, value.runtime_generation);
  writer.boolean(value.current);
  writer.boolean(value.known);
  writer.str(value.explanation, limits.max_string_bytes);
}

ResolvedCapability read_resolved_capability(ByteReader& reader, const RegistryLimits& limits) {
  ResolvedCapability value;
  value.device = read_id<DeviceIdTag>(reader);
  value.device_generation = read_gen<DeviceGenerationTag>(reader);
  value.device_boot = read_gen<DeviceBootTag>(reader);
  value.capability = read_id<CapabilityIdTag>(reader);
  value.schema = read_id<CapabilitySchemaIdTag>(reader);
  value.capability_generation = read_gen<CapabilityGenerationTag>(reader);
  const std::uint16_t support = reader.u16();
  const std::uint16_t effective = reader.u16();
  if (support > static_cast<std::uint16_t>(SupportState::RevalidationRequired) ||
      effective > static_cast<std::uint16_t>(EffectiveSupport::RevalidationRequired)) {
    reader.fail(ErrorCode::InvalidSupportState, "unknown support state on the wire");
    return value;
  }
  value.declared_support = static_cast<SupportState>(support);
  value.effective_support = static_cast<EffectiveSupport>(effective);
  value.value = read_value(reader, limits, 1);
  const std::uint16_t precision = reader.u16();
  const std::uint16_t truth = reader.u16();
  const std::uint16_t contradiction = reader.u16();
  if (precision > static_cast<std::uint16_t>(PrecisionClass::Unknown) ||
      truth > static_cast<std::uint16_t>(TruthClass::Unknown) ||
      contradiction > static_cast<std::uint16_t>(ContradictionState::RevalidationRequired)) {
    reader.fail(ErrorCode::CorruptData, "unknown classification on the wire");
    return value;
  }
  value.precision = static_cast<PrecisionClass>(precision);
  value.truth = static_cast<TruthClass>(truth);
  value.contradiction = static_cast<ContradictionState>(contradiction);
  value.staleness = static_cast<StalenessFlag>(reader.u32());
  value.conditions = read_condition(reader, limits, 1);
  value.condition_evaluation = read_condition_evaluation(reader, limits);
  const std::uint32_t quirks = reader.u32();
  if (!reader.ok()) return value;
  if (quirks > limits.max_query_results || quirks > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "quirk application count exceeds the bound");
    return value;
  }
  for (std::uint32_t i = 0; i < quirks; ++i) {
    value.quirks.push_back(read_quirk_application(reader, limits));
    if (!reader.ok()) return value;
  }
  const std::uint32_t evidence = reader.u32();
  if (!reader.ok()) return value;
  if (evidence > limits.max_query_results || evidence > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "evidence view count exceeds the bound");
    return value;
  }
  for (std::uint32_t i = 0; i < evidence; ++i) {
    value.evidence.push_back(read_evidence_view(reader, limits));
    if (!reader.ok()) return value;
  }
  value.firmware_generation = read_gen<FirmwareGenerationTag>(reader);
  value.driver_generation = read_gen<DriverGenerationTag>(reader);
  value.runtime_generation = read_gen<RuntimeGenerationTag>(reader);
  value.current = reader.boolean();
  value.known = reader.boolean();
  value.explanation = reader.str(limits.max_string_bytes);
  return value;
}

void write_device_comparison(ByteWriter& writer, const DeviceComparison& value, const RegistryLimits& limits) {
  write_id(writer, value.left);
  write_id(writer, value.right);
  write_gen(writer, value.left_generation);
  write_gen(writer, value.right_generation);
  writer.str(value.left_software, limits.max_name_length);
  writer.str(value.right_software, limits.max_name_length);
  const std::size_t count = std::min(value.differences.size(), limits.max_comparison_entries);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    const CapabilityDifference& difference = value.differences[i];
    write_id(writer, difference.capability);
    writer.u16(static_cast<std::uint16_t>(difference.kind));
    writer.u16(static_cast<std::uint16_t>(difference.left_effective));
    writer.u16(static_cast<std::uint16_t>(difference.right_effective));
    writer.str(difference.detail, limits.max_metadata_bytes);
  }
  writer.u32(static_cast<std::uint32_t>(std::min(value.equivalent.size(), limits.max_comparison_entries)));
  for (std::size_t i = 0; i < value.equivalent.size() && i < limits.max_comparison_entries; ++i) {
    write_id(writer, value.equivalent[i]);
  }
  writer.str(value.explanation, limits.max_string_bytes);
}

DeviceComparison read_device_comparison(ByteReader& reader, const RegistryLimits& limits) {
  DeviceComparison value;
  value.left = read_id<DeviceIdTag>(reader);
  value.right = read_id<DeviceIdTag>(reader);
  value.left_generation = read_gen<DeviceGenerationTag>(reader);
  value.right_generation = read_gen<DeviceGenerationTag>(reader);
  value.left_software = reader.str(limits.max_name_length);
  value.right_software = reader.str(limits.max_name_length);
  const std::uint32_t count = reader.u32();
  if (!reader.ok()) return value;
  if (count > limits.max_comparison_entries || count > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "comparison entry count exceeds the bound");
    return value;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    CapabilityDifference difference;
    difference.capability = read_id<CapabilityIdTag>(reader);
    const std::uint16_t kind = reader.u16();
    const std::uint16_t left = reader.u16();
    const std::uint16_t right = reader.u16();
    if (kind > static_cast<std::uint16_t>(CapabilityDifferenceKind::ShapeDiffers) ||
        left > static_cast<std::uint16_t>(EffectiveSupport::RevalidationRequired) ||
        right > static_cast<std::uint16_t>(EffectiveSupport::RevalidationRequired)) {
      reader.fail(ErrorCode::CorruptData, "unknown difference enumeration on the wire");
      return value;
    }
    difference.kind = static_cast<CapabilityDifferenceKind>(kind);
    difference.left_effective = static_cast<EffectiveSupport>(left);
    difference.right_effective = static_cast<EffectiveSupport>(right);
    difference.detail = reader.str(limits.max_metadata_bytes);
    value.differences.push_back(std::move(difference));
  }
  const std::uint32_t equivalent = reader.u32();
  if (!reader.ok()) return value;
  if (equivalent > limits.max_comparison_entries || equivalent > reader.remaining()) {
    reader.fail(ErrorCode::LimitExceeded, "equivalent capability count exceeds the bound");
    return value;
  }
  for (std::uint32_t i = 0; i < equivalent; ++i) value.equivalent.push_back(read_id<CapabilityIdTag>(reader));
  value.explanation = reader.str(limits.max_string_bytes);
  return value;
}

void write_registry_snapshot(ByteWriter& writer, const RegistrySnapshot& value, const RegistryLimits& limits) {
  write_gen(writer, value.generation);
  write_gen(writer, value.epoch);
  writer.u64(value.state_revision);
  writer.str(value.created_at_utc, limits.max_metadata_bytes);
  writer.u32(static_cast<std::uint32_t>(std::min(value.subjects.size(), limits.max_snapshot_records)));
  for (std::size_t i = 0; i < value.subjects.size() && i < limits.max_snapshot_records; ++i) {
    write_subject_snapshot(writer, value.subjects[i], limits);
  }
  writer.u32(static_cast<std::uint32_t>(std::min(value.capabilities.size(), limits.max_snapshot_records)));
  for (std::size_t i = 0; i < value.capabilities.size() && i < limits.max_snapshot_records; ++i) {
    write_resolved_capability(writer, value.capabilities[i], limits);
  }
  writer.u32(static_cast<std::uint32_t>(std::min(value.quirks.size(), limits.max_quirks)));
  for (std::size_t i = 0; i < value.quirks.size() && i < limits.max_quirks; ++i) {
    write_quirk_record(writer, value.quirks[i], limits);
  }
  writer.u32(static_cast<std::uint32_t>(std::min(value.compatibility.size(), limits.max_compatibility_facts)));
  for (std::size_t i = 0; i < value.compatibility.size() && i < limits.max_compatibility_facts; ++i) {
    write_compatibility_fact(writer, value.compatibility[i], limits);
  }
  writer.u32(static_cast<std::uint32_t>(std::min(value.publishers.size(), limits.max_publishers)));
  for (std::size_t i = 0; i < value.publishers.size() && i < limits.max_publishers; ++i) {
    write_publisher_record(writer, value.publishers[i], limits);
  }
  writer.u64(value.real_statements);
  writer.u64(value.synthetic_statements);
  writer.u64(value.unsupported_statements);
  writer.u64(value.unknown_statements);
  writer.u64(value.stale_evidence_records);
  writer.u64(value.canonical_digest);
}

RegistrySnapshot read_registry_snapshot(ByteReader& reader, const RegistryLimits& limits) {
  RegistrySnapshot value;
  value.generation = read_gen<SnapshotGenerationTag>(reader);
  value.epoch = read_gen<CoordinatorEpochTag>(reader);
  value.state_revision = reader.u64();
  value.created_at_utc = reader.str(limits.max_metadata_bytes);
  const auto read_count = [&reader, &limits](std::size_t bound, const char* what) -> std::uint32_t {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) return 0;
    if (count > bound || count > reader.remaining()) {
      reader.fail(ErrorCode::LimitExceeded, std::string(what) + " count exceeds the bound");
      return 0;
    }
    return count;
  };
  const std::uint32_t subjects = read_count(limits.max_snapshot_records, "snapshot subject");
  for (std::uint32_t i = 0; i < subjects && reader.ok(); ++i) {
    value.subjects.push_back(read_subject_snapshot(reader, limits));
  }
  const std::uint32_t capabilities = read_count(limits.max_snapshot_records, "snapshot capability");
  for (std::uint32_t i = 0; i < capabilities && reader.ok(); ++i) {
    value.capabilities.push_back(read_resolved_capability(reader, limits));
  }
  const std::uint32_t quirks = read_count(limits.max_quirks, "snapshot quirk");
  for (std::uint32_t i = 0; i < quirks && reader.ok(); ++i) {
    value.quirks.push_back(read_quirk_record(reader, limits));
  }
  const std::uint32_t compatibility = read_count(limits.max_compatibility_facts, "snapshot compatibility");
  for (std::uint32_t i = 0; i < compatibility && reader.ok(); ++i) {
    value.compatibility.push_back(read_compatibility_fact(reader, limits));
  }
  const std::uint32_t publishers = read_count(limits.max_publishers, "snapshot publisher");
  for (std::uint32_t i = 0; i < publishers && reader.ok(); ++i) {
    value.publishers.push_back(read_publisher_record(reader, limits));
  }
  value.real_statements = static_cast<std::size_t>(reader.u64());
  value.synthetic_statements = static_cast<std::size_t>(reader.u64());
  value.unsupported_statements = static_cast<std::size_t>(reader.u64());
  value.unknown_statements = static_cast<std::size_t>(reader.u64());
  value.stale_evidence_records = static_cast<std::size_t>(reader.u64());
  value.canonical_digest = reader.u64();
  return value;
}

void write_publication(ByteWriter& writer, const Publication& publication, const RegistryLimits& limits) {
  writer.u16(static_cast<std::uint16_t>(publication.kind));
  write_publisher_identity(writer, publication.publisher);
  writer.u64(publication.sequence);
  write_gen(writer, publication.evidence_generation);
  write_device_registration(writer, publication.device, limits);
  write_capability_record(writer, publication.capability, limits);
  write_quirk_record(writer, publication.quirk, limits);
  write_compatibility_fact(writer, publication.compatibility, limits);
  write_capability_schema(writer, publication.schema, limits);
  writer.str(publication.reason, limits.max_metadata_bytes);
  writer.str(publication.issued_at_utc, limits.max_metadata_bytes);
}

Publication read_publication(ByteReader& reader, const RegistryLimits& limits) {
  Publication publication;
  const std::uint16_t kind = reader.u16();
  if (kind > static_cast<std::uint16_t>(PublicationKind::Heartbeat)) {
    reader.fail(ErrorCode::CorruptData, "unknown publication kind on the wire");
    return publication;
  }
  publication.kind = static_cast<PublicationKind>(kind);
  publication.publisher = read_publisher_identity(reader);
  publication.sequence = reader.u64();
  publication.evidence_generation = read_gen<EvidenceGenerationTag>(reader);
  publication.device = read_device_registration(reader, limits);
  publication.capability = read_capability_record(reader, limits);
  publication.quirk = read_quirk_record(reader, limits);
  publication.compatibility = read_compatibility_fact(reader, limits);
  publication.schema = read_capability_schema(reader, limits);
  publication.reason = reader.str(limits.max_metadata_bytes);
  publication.issued_at_utc = reader.str(limits.max_metadata_bytes);
  return publication;
}

void write_capability_query(ByteWriter& writer, const CapabilityQuery& query, const RegistryLimits& limits) {
  write_id(writer, query.device);
  write_gen(writer, query.device_generation);
  write_id(writer, query.capability);
  write_environment(writer, query.environment, limits);
  writer.boolean(query.include_evidence);
}

CapabilityQuery read_capability_query(ByteReader& reader, const RegistryLimits& limits) {
  CapabilityQuery query;
  query.device = read_id<DeviceIdTag>(reader);
  query.device_generation = read_gen<DeviceGenerationTag>(reader);
  query.capability = read_id<CapabilityIdTag>(reader);
  query.environment = read_environment(reader, limits);
  query.include_evidence = reader.boolean();
  return query;
}

void write_device_capability_query(ByteWriter& writer, const DeviceCapabilityQuery& query, const RegistryLimits& limits) {
  write_id(writer, query.device);
  write_gen(writer, query.device_generation);
  write_environment(writer, query.environment, limits);
  writer.str(query.capability_prefix, limits.max_name_length);
  writer.boolean(query.domain.has_value());
  if (query.domain.has_value()) writer.u16(static_cast<std::uint16_t>(*query.domain));
  writer.boolean(query.declared_filter.has_value());
  if (query.declared_filter.has_value()) writer.u16(static_cast<std::uint16_t>(*query.declared_filter));
  writer.boolean(query.effective_filter.has_value());
  if (query.effective_filter.has_value()) writer.u16(static_cast<std::uint16_t>(*query.effective_filter));
  writer.boolean(query.truth_filter.has_value());
  if (query.truth_filter.has_value()) writer.u16(static_cast<std::uint16_t>(*query.truth_filter));
  writer.boolean(query.only_conditional);
  writer.boolean(query.only_unknown);
  writer.boolean(query.only_unsupported);
  writer.boolean(query.only_stale);
  writer.boolean(query.only_current);
  writer.u64(query.max_results);
}

DeviceCapabilityQuery read_device_capability_query(ByteReader& reader, const RegistryLimits& limits) {
  DeviceCapabilityQuery query;
  query.device = read_id<DeviceIdTag>(reader);
  query.device_generation = read_gen<DeviceGenerationTag>(reader);
  query.environment = read_environment(reader, limits);
  query.capability_prefix = reader.str(limits.max_name_length);
  if (reader.boolean()) {
    const std::uint16_t domain = reader.u16();
    if (domain > static_cast<std::uint16_t>(CapabilityDomain::Platform)) {
      reader.fail(ErrorCode::InvalidCapability, "unknown capability domain on the wire");
      return query;
    }
    query.domain = static_cast<CapabilityDomain>(domain);
  }
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(SupportState::RevalidationRequired)) {
      reader.fail(ErrorCode::InvalidSupportState, "unknown support state on the wire");
      return query;
    }
    query.declared_filter = static_cast<SupportState>(value);
  }
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(EffectiveSupport::RevalidationRequired)) {
      reader.fail(ErrorCode::InvalidSupportState, "unknown effective support on the wire");
      return query;
    }
    query.effective_filter = static_cast<EffectiveSupport>(value);
  }
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(TruthClass::Unknown)) {
      reader.fail(ErrorCode::CorruptData, "unknown truth class on the wire");
      return query;
    }
    query.truth_filter = static_cast<TruthClass>(value);
  }
  query.only_conditional = reader.boolean();
  query.only_unknown = reader.boolean();
  query.only_unsupported = reader.boolean();
  query.only_stale = reader.boolean();
  query.only_current = reader.boolean();
  query.max_results = static_cast<std::size_t>(reader.u64());
  return query;
}

void write_fleet_query(ByteWriter& writer, const FleetCapabilityQuery& query, const RegistryLimits& limits) {
  write_id(writer, query.capability);
  write_environment(writer, query.environment, limits);
  writer.boolean(query.effective_filter.has_value());
  if (query.effective_filter.has_value()) writer.u16(static_cast<std::uint16_t>(*query.effective_filter));
  writer.boolean(query.truth_filter.has_value());
  if (query.truth_filter.has_value()) writer.u16(static_cast<std::uint16_t>(*query.truth_filter));
  writer.boolean(query.hardware_class.has_value());
  if (query.hardware_class.has_value()) writer.u16(static_cast<std::uint16_t>(*query.hardware_class));
  writer.boolean(query.only_current);
  writer.u64(query.max_results);
}

FleetCapabilityQuery read_fleet_query(ByteReader& reader, const RegistryLimits& limits) {
  FleetCapabilityQuery query;
  query.capability = read_id<CapabilityIdTag>(reader);
  query.environment = read_environment(reader, limits);
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(EffectiveSupport::RevalidationRequired)) {
      reader.fail(ErrorCode::InvalidSupportState, "unknown effective support on the wire");
      return query;
    }
    query.effective_filter = static_cast<EffectiveSupport>(value);
  }
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(TruthClass::Unknown)) {
      reader.fail(ErrorCode::CorruptData, "unknown truth class on the wire");
      return query;
    }
    query.truth_filter = static_cast<TruthClass>(value);
  }
  if (reader.boolean()) {
    const std::uint16_t value = reader.u16();
    if (value > static_cast<std::uint16_t>(HardwareClass::Other)) {
      reader.fail(ErrorCode::InvalidIdentity, "unknown hardware class on the wire");
      return query;
    }
    query.hardware_class = static_cast<HardwareClass>(value);
  }
  query.only_current = reader.boolean();
  query.max_results = static_cast<std::size_t>(reader.u64());
  return query;
}

void write_stale_query(ByteWriter& writer, const StaleCapabilityQuery& query, const RegistryLimits& limits) {
  write_id(writer, query.device);
  write_gen(writer, query.device_generation);
  write_environment(writer, query.environment, limits);
  writer.u64(query.max_results);
}

StaleCapabilityQuery read_stale_query(ByteReader& reader, const RegistryLimits& limits) {
  StaleCapabilityQuery query;
  query.device = read_id<DeviceIdTag>(reader);
  query.device_generation = read_gen<DeviceGenerationTag>(reader);
  query.environment = read_environment(reader, limits);
  query.max_results = static_cast<std::size_t>(reader.u64());
  return query;
}

void write_quirk_query(ByteWriter& writer, const QuirkQuery& query, const RegistryLimits& limits) {
  writer.boolean(query.device.has_value());
  if (query.device.has_value()) write_id(writer, *query.device);
  write_environment(writer, query.environment, limits);
  writer.boolean(query.capability.has_value());
  if (query.capability.has_value()) write_id(writer, *query.capability);
  writer.boolean(query.only_applicable);
  writer.u64(query.max_results);
}

QuirkQuery read_quirk_query(ByteReader& reader, const RegistryLimits& limits) {
  QuirkQuery query;
  if (reader.boolean()) query.device = read_id<DeviceIdTag>(reader);
  query.environment = read_environment(reader, limits);
  if (reader.boolean()) query.capability = read_id<CapabilityIdTag>(reader);
  query.only_applicable = reader.boolean();
  query.max_results = static_cast<std::size_t>(reader.u64());
  return query;
}

void write_compatibility_query(ByteWriter& writer, const CompatibilityQuery& query, const RegistryLimits& limits) {
  writer.boolean(query.reference.has_value());
  if (query.reference.has_value()) write_hardware_reference(writer, *query.reference, limits);
  writer.boolean(query.kind.has_value());
  if (query.kind.has_value()) writer.u16(static_cast<std::uint16_t>(*query.kind));
  writer.boolean(query.include_withdrawn);
  writer.u64(query.max_results);
}

CompatibilityQuery read_compatibility_query(ByteReader& reader, const RegistryLimits& limits) {
  CompatibilityQuery query;
  if (reader.boolean()) query.reference = read_hardware_reference(reader, limits);
  if (reader.boolean()) {
    const std::uint16_t kind = reader.u16();
    if (kind > static_cast<std::uint16_t>(CompatibilitySubjectKind::Other)) {
      reader.fail(ErrorCode::CorruptData, "unknown compatibility subject kind on the wire");
      return query;
    }
    query.kind = static_cast<CompatibilitySubjectKind>(kind);
  }
  query.include_withdrawn = reader.boolean();
  query.max_results = static_cast<std::size_t>(reader.u64());
  return query;
}

void write_comparison_request(ByteWriter& writer, const ComparisonRequest& request, const RegistryLimits& limits) {
  write_id(writer, request.left);
  write_id(writer, request.right);
  write_gen(writer, request.left_generation);
  write_gen(writer, request.right_generation);
  write_environment(writer, request.environment, limits);
}

ComparisonRequest read_comparison_request(ByteReader& reader, const RegistryLimits& limits) {
  ComparisonRequest request;
  request.left = read_id<DeviceIdTag>(reader);
  request.right = read_id<DeviceIdTag>(reader);
  request.left_generation = read_gen<DeviceGenerationTag>(reader);
  request.right_generation = read_gen<DeviceGenerationTag>(reader);
  request.environment = read_environment(reader, limits);
  return request;
}

void write_publisher_query(ByteWriter& writer, const PublisherQuery& query, const RegistryLimits& limits) {
  writer.boolean(query.state.has_value());
  if (query.state.has_value()) writer.u16(static_cast<std::uint16_t>(*query.state));
  writer.u64(query.max_results);
  (void)limits;
}

PublisherQuery read_publisher_query(ByteReader& reader, const RegistryLimits& limits) {
  PublisherQuery query;
  if (reader.boolean()) {
    const std::uint16_t state = reader.u16();
    if (state > static_cast<std::uint16_t>(PublisherState::Dead)) {
      reader.fail(ErrorCode::CorruptData, "unknown publisher state on the wire");
      return query;
    }
    query.state = static_cast<PublisherState>(state);
  }
  query.max_results = static_cast<std::size_t>(reader.u64());
  (void)limits;
  return query;
}

}  // namespace hcr::detail
