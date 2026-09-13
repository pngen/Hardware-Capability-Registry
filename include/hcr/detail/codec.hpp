// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Internal canonical codecs shared by the persistence and protocol layers.
// Not part of the supported public API.
#pragma once

#include <cstdint>
#include <vector>

#include "hcr/capability.hpp"
#include "hcr/compatibility.hpp"
#include "hcr/detail/binary.hpp"
#include "hcr/durable_state.hpp"
#include "hcr/limits.hpp"
#include "hcr/publication.hpp"
#include "hcr/query.hpp"
#include "hcr/snapshot.hpp"
#include "hcr/status.hpp"

namespace hcr::detail {

void write_publisher_identity(ByteWriter& writer, const PublisherIdentity& identity);
PublisherIdentity read_publisher_identity(ByteReader& reader);

void write_condition(ByteWriter& writer, const ConditionNode* node, const RegistryLimits& limits);
ConditionPtr read_condition(ByteReader& reader, const RegistryLimits& limits, std::size_t depth);

void write_value(ByteWriter& writer, const CapabilityValue& value, const RegistryLimits& limits);
CapabilityValue read_value(ByteReader& reader, const RegistryLimits& limits, std::size_t depth);

void write_environment(ByteWriter& writer, const EnvironmentContext& environment, const RegistryLimits& limits);
EnvironmentContext read_environment(ByteReader& reader, const RegistryLimits& limits);

void write_evidence_source(ByteWriter& writer, const EvidenceSource& source, const RegistryLimits& limits);
EvidenceSource read_evidence_source(ByteReader& reader, const RegistryLimits& limits);

void write_hardware_identity(ByteWriter& writer, const HardwareIdentity& identity, const RegistryLimits& limits);
HardwareIdentity read_hardware_identity(ByteReader& reader, const RegistryLimits& limits);

void write_platform_identity(ByteWriter& writer, const PlatformIdentity& identity, const RegistryLimits& limits);
PlatformIdentity read_platform_identity(ByteReader& reader, const RegistryLimits& limits);

void write_device_identity(ByteWriter& writer, const DeviceIdentity& identity, const RegistryLimits& limits);
DeviceIdentity read_device_identity(ByteReader& reader, const RegistryLimits& limits);

void write_software_state(ByteWriter& writer, const DeviceSoftwareState& state, const RegistryLimits& limits);
DeviceSoftwareState read_software_state(ByteReader& reader, const RegistryLimits& limits);

void write_device_registration(ByteWriter& writer, const DeviceRegistration& registration, const RegistryLimits& limits);
DeviceRegistration read_device_registration(ByteReader& reader, const RegistryLimits& limits);

void write_capability_schema(ByteWriter& writer, const CapabilitySchema& schema, const RegistryLimits& limits);
CapabilitySchema read_capability_schema(ByteReader& reader, const RegistryLimits& limits);

void write_capability_record(ByteWriter& writer, const CapabilityRecord& record, const RegistryLimits& limits);
CapabilityRecord read_capability_record(ByteReader& reader, const RegistryLimits& limits);

void write_quirk_record(ByteWriter& writer, const QuirkRecord& quirk, const RegistryLimits& limits);
QuirkRecord read_quirk_record(ByteReader& reader, const RegistryLimits& limits);

void write_compatibility_fact(ByteWriter& writer, const CompatibilityFact& fact, const RegistryLimits& limits);
CompatibilityFact read_compatibility_fact(ByteReader& reader, const RegistryLimits& limits);

void write_subject_snapshot(ByteWriter& writer, const SubjectSnapshot& subject, const RegistryLimits& limits);
SubjectSnapshot read_subject_snapshot(ByteReader& reader, const RegistryLimits& limits);

void write_publisher_record(ByteWriter& writer, const PublisherRecord& record, const RegistryLimits& limits);
PublisherRecord read_publisher_record(ByteReader& reader, const RegistryLimits& limits);

void write_evidence_view(ByteWriter& writer, const EvidenceView& view, const RegistryLimits& limits);
EvidenceView read_evidence_view(ByteReader& reader, const RegistryLimits& limits);

void write_quirk_application(ByteWriter& writer, const QuirkApplication& application, const RegistryLimits& limits);
QuirkApplication read_quirk_application(ByteReader& reader, const RegistryLimits& limits);

void write_resolved_capability(ByteWriter& writer, const ResolvedCapability& value, const RegistryLimits& limits);
ResolvedCapability read_resolved_capability(ByteReader& reader, const RegistryLimits& limits);

void write_device_comparison(ByteWriter& writer, const DeviceComparison& value, const RegistryLimits& limits);
DeviceComparison read_device_comparison(ByteReader& reader, const RegistryLimits& limits);

void write_registry_snapshot(ByteWriter& writer, const RegistrySnapshot& value, const RegistryLimits& limits);
RegistrySnapshot read_registry_snapshot(ByteReader& reader, const RegistryLimits& limits);

void write_publication(ByteWriter& writer, const Publication& publication, const RegistryLimits& limits);
Publication read_publication(ByteReader& reader, const RegistryLimits& limits);

void write_hardware_reference(ByteWriter& writer, const HardwareReference& reference, const RegistryLimits& limits);
HardwareReference read_hardware_reference(ByteReader& reader, const RegistryLimits& limits);

void write_capability_query(ByteWriter& writer, const CapabilityQuery& query, const RegistryLimits& limits);
CapabilityQuery read_capability_query(ByteReader& reader, const RegistryLimits& limits);

void write_device_capability_query(ByteWriter& writer, const DeviceCapabilityQuery& query, const RegistryLimits& limits);
DeviceCapabilityQuery read_device_capability_query(ByteReader& reader, const RegistryLimits& limits);

void write_fleet_query(ByteWriter& writer, const FleetCapabilityQuery& query, const RegistryLimits& limits);
FleetCapabilityQuery read_fleet_query(ByteReader& reader, const RegistryLimits& limits);

void write_stale_query(ByteWriter& writer, const StaleCapabilityQuery& query, const RegistryLimits& limits);
StaleCapabilityQuery read_stale_query(ByteReader& reader, const RegistryLimits& limits);

void write_quirk_query(ByteWriter& writer, const QuirkQuery& query, const RegistryLimits& limits);
QuirkQuery read_quirk_query(ByteReader& reader, const RegistryLimits& limits);

void write_compatibility_query(ByteWriter& writer, const CompatibilityQuery& query, const RegistryLimits& limits);
CompatibilityQuery read_compatibility_query(ByteReader& reader, const RegistryLimits& limits);

void write_comparison_request(ByteWriter& writer, const ComparisonRequest& request, const RegistryLimits& limits);
ComparisonRequest read_comparison_request(ByteReader& reader, const RegistryLimits& limits);

void write_publisher_query(ByteWriter& writer, const PublisherQuery& query, const RegistryLimits& limits);
PublisherQuery read_publisher_query(ByteReader& reader, const RegistryLimits& limits);

}  // namespace hcr::detail
