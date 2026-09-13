// Hardware Capability Registry - core type and evaluation tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/canonical.hpp"
#include "hcr/catalog.hpp"
#include "hcr/condition.hpp"
#include "hcr/software_version.hpp"
#include "hcr/value.hpp"

using namespace hcr;
using namespace hcrtest;

HCR_TEST(identity, canonical_tokens_are_enforced) {
  HCR_CHECK(is_canonical_token("dev.gpu.abc-123"));
  HCR_CHECK(is_canonical_token("a"));
  HCR_CHECK(!is_canonical_token(""));
  HCR_CHECK(!is_canonical_token("Upper"));
  HCR_CHECK(!is_canonical_token("-leading"));
  HCR_CHECK(!is_canonical_token("has space"));
  HCR_CHECK(!is_canonical_token(std::string(200, 'a')));
  HCR_CHECK(!DeviceId::parse("Bad Token").valid());
  HCR_CHECK(DeviceId::parse("dev.ok").valid());
}

HCR_TEST(identity, generation_counters_never_regress_implicitly) {
  DeviceGeneration generation = DeviceGeneration::first();
  HCR_CHECK(generation.valid());
  HCR_CHECK(generation.next() > generation);
  HCR_CHECK(!DeviceGeneration{}.valid());
}

HCR_TEST(version, comparison_is_numeric_component_wise) {
  HCR_CHECK(SoftwareVersion::parse("1.2.3") == SoftwareVersion::parse("1.2.3"));
  HCR_CHECK(SoftwareVersion::parse("1.10") > SoftwareVersion::parse("1.9"));
  HCR_CHECK(SoftwareVersion::parse("580.65.06") > SoftwareVersion::parse("580.6"));
  HCR_CHECK(SoftwareVersion::parse("1.2") == SoftwareVersion::parse("1.2.0"));
  HCR_CHECK(SoftwareVersion::parse("6.14.0-24-generic") > SoftwareVersion::parse("6.14.0"));
  HCR_CHECK(!SoftwareVersion::parse("").valid());
}

HCR_TEST(version, range_containment) {
  VersionRange range;
  range.minimum = SoftwareVersion::parse("1.0");
  range.maximum = SoftwareVersion::parse("2.0");
  HCR_CHECK(range.valid());
  HCR_CHECK(range.contains(SoftwareVersion::parse("1.5")));
  HCR_CHECK(range.contains(SoftwareVersion::parse("1.0")));
  HCR_CHECK(!range.contains(SoftwareVersion::parse("2.0")));
  HCR_CHECK(!range.contains(SoftwareVersion::parse("0.9")));
  VersionRange inverted;
  inverted.minimum = SoftwareVersion::parse("2.0");
  inverted.maximum = SoftwareVersion::parse("1.0");
  HCR_CHECK(!inverted.valid());
}

HCR_TEST(support, states_are_distinct_and_round_trip) {
  const SupportState states[] = {SupportState::SupportedNative,
                                 SupportState::SupportedConditional,
                                 SupportState::SupportedEmulated,
                                 SupportState::SupportedSoftwareAssisted,
                                 SupportState::SupportedExperimental,
                                 SupportState::Disabled,
                                 SupportState::Unsupported,
                                 SupportState::Unknown,
                                 SupportState::RevalidationRequired};
  for (const SupportState state : states) {
    SupportState parsed = SupportState::Unknown;
    HCR_CHECK(support_state_from_string(to_string(state), parsed));
    HCR_CHECK(parsed == state);
  }
  HCR_CHECK(asserts_capability_present(SupportState::SupportedNative));
  HCR_CHECK(asserts_capability_present(SupportState::Disabled));
  HCR_CHECK(!asserts_capability_present(SupportState::Unknown));
  HCR_CHECK(!asserts_capability_present(SupportState::Unsupported));
  HCR_CHECK(asserts_capability_absent(SupportState::Unsupported));
  HCR_CHECK(!asserts_capability_absent(SupportState::Unknown));
  HCR_CHECK(!asserts_capability_absent(SupportState::SupportedNative));
}

HCR_TEST(provenance, authority_orders_live_above_static_above_synthetic) {
  HCR_CHECK(source_authority(SourceClass::HardwareProbe) > source_authority(SourceClass::LiveVendorApi));
  HCR_CHECK(source_authority(SourceClass::LiveVendorApi) > source_authority(SourceClass::Nvml));
  HCR_CHECK(source_authority(SourceClass::Nvml) > source_authority(SourceClass::OsEnumeration));
  HCR_CHECK(source_authority(SourceClass::OsEnumeration) >
            source_authority(SourceClass::StaticCuratedDatabase));
  HCR_CHECK(source_authority(SourceClass::StaticCuratedDatabase) >
            source_authority(SourceClass::ImportedManifest));
  HCR_CHECK(source_authority(SourceClass::ImportedManifest) >
            source_authority(SourceClass::SyntheticBackend));
  HCR_CHECK(provenance_is_real(ProvenanceClass::RealLiveHardware));
  HCR_CHECK(provenance_is_real(ProvenanceClass::RealOsReported));
  HCR_CHECK(!provenance_is_real(ProvenanceClass::Synthetic));
  HCR_CHECK(!provenance_is_real(ProvenanceClass::CuratedStatic));
  HCR_CHECK(provenance_is_durable(ProvenanceClass::CuratedStatic));
  HCR_CHECK(provenance_is_durable(ProvenanceClass::Imported));
  HCR_CHECK(!provenance_is_durable(ProvenanceClass::RealVendorApi));
  HCR_CHECK(!provenance_is_durable(ProvenanceClass::RealRuntimeProbe));
  HCR_CHECK(!provenance_is_durable(ProvenanceClass::Synthetic));
}

HCR_TEST(provenance, a_source_may_not_strengthen_its_claim) {
  HCR_CHECK(provenance_allowed_for(ProvenanceClass::CuratedStatic, SourceClass::StaticCuratedDatabase));
  HCR_CHECK(!provenance_allowed_for(ProvenanceClass::RealLiveHardware, SourceClass::StaticCuratedDatabase));
  HCR_CHECK(!provenance_allowed_for(ProvenanceClass::RealVendorApi, SourceClass::SyntheticBackend));
  HCR_CHECK(provenance_allowed_for(ProvenanceClass::Synthetic, SourceClass::SyntheticBackend));
  HCR_CHECK(provenance_allowed_for(ProvenanceClass::UnknownSource, SourceClass::Nvml));
  HCR_CHECK(provenance_allowed_for(ProvenanceClass::CuratedStatic, SourceClass::Nvml));
}

HCR_TEST(catalog, every_schema_is_valid_and_unique) {
  std::map<std::string, bool> seen;
  HCR_CHECK(!builtin_capability_schemas().empty());
  for (const CapabilitySchema& schema : builtin_capability_schemas()) {
    HCR_CHECK_MSG(validate_capability_schema(schema, RegistryLimits{}).ok(), schema.id.str());
    HCR_CHECK(seen.find(schema.id.str()) == seen.end());
    seen[schema.id.str()] = true;
    HCR_CHECK(schema.value_kind != ValueKind::None);
    HCR_CHECK(find_builtin_schema_by_capability(schema.capability) != nullptr);
  }
}

HCR_TEST(values, typed_forms_validate_and_normalize) {
  RegistryLimits limits;
  CapabilityValue feature_set(FeatureSetValue{{"b", "a", "a"}});
  HCR_CHECK_STATUS(normalize_value(feature_set, limits));
  HCR_CHECK_EQ(feature_set.describe(), std::string("{a,b}"));

  CapabilityValue unsorted(FeatureSetValue{{"b", "a"}});
  HCR_CHECK(!validate_value(unsorted, limits).ok());

  CapabilityValue bytes_value(BytesValue{1024});
  HCR_CHECK(validate_value(bytes_value, limits).ok());
  HCR_CHECK_EQ(bytes_value.describe(), std::string("1024 B"));

  CapabilityValue enumeration(EnumValue{"memory_type", "hbm3"});
  HCR_CHECK(validate_value(enumeration, limits).ok());
  HCR_CHECK_EQ(enumeration.describe(), std::string("memory_type:hbm3"));

  CapabilityValue bad_enumeration(EnumValue{"Memory Type", "hbm3"});
  HCR_CHECK(!validate_value(bad_enumeration, limits).ok());

  NumericRangeValue inverted_range;
  inverted_range.minimum = 10;
  inverted_range.maximum = 1;
  HCR_CHECK(!validate_value(CapabilityValue(inverted_range), limits).ok());

  NumericRangeValue zero_step;
  zero_step.minimum = 0;
  zero_step.maximum = 10;
  zero_step.step = 0;
  HCR_CHECK(!validate_value(CapabilityValue(zero_step), limits).ok());

  VersionRangeValue open_range;
  HCR_CHECK(!validate_value(CapabilityValue(open_range), limits).ok());
}

HCR_TEST(values, records_are_bounded_and_canonical) {
  RegistryLimits limits;
  RecordValue record;
  RecordField second;
  second.name = "b";
  second.value = std::make_shared<const CapabilityValue>(CapabilityValue(true));
  RecordField first;
  first.name = "a";
  first.value = std::make_shared<const CapabilityValue>(CapabilityValue(CountValue{3, "units"}));
  record.fields.push_back(second);
  record.fields.push_back(first);
  CapabilityValue value(record);
  HCR_CHECK(!validate_value(value, limits).ok());
  HCR_CHECK_STATUS(normalize_value(value, limits));
  HCR_CHECK_EQ(value.describe(), std::string("{a=3 units,b=true}"));

  RecordValue deep;
  std::shared_ptr<const CapabilityValue> current =
      std::make_shared<const CapabilityValue>(CapabilityValue(true));
  for (int i = 0; i < 8; ++i) {
    RecordValue nested;
    RecordField field;
    field.name = "n";
    field.value = current;
    nested.fields.push_back(field);
    current = std::make_shared<const CapabilityValue>(CapabilityValue(nested));
  }
  HCR_CHECK(!validate_value(*current, limits).ok());
}

HCR_TEST(conditions, absent_facts_are_unknown_never_satisfied) {
  const auto node = min_driver("580.0");
  EnvironmentContext empty;
  const ConditionEvaluation missing = evaluate_condition(node.get(), empty);
  HCR_CHECK(missing.outcome == ConditionOutcome::Unknown);

  EnvironmentContext satisfied;
  satisfied.set_driver_version("580.10");
  HCR_CHECK(evaluate_condition(node.get(), satisfied).outcome == ConditionOutcome::Satisfied);

  EnvironmentContext unsatisfied;
  unsatisfied.set_driver_version("570.0");
  HCR_CHECK(evaluate_condition(node.get(), unsatisfied).outcome == ConditionOutcome::Unsatisfied);
}

HCR_TEST(conditions, logical_composition_is_deterministic) {
  EnvironmentContext environment;
  environment.set_driver_version("600.0");
  environment.set_os("linux");

  const auto conjunction = all_of({min_driver("580.0"), required_os("linux")});
  HCR_CHECK(evaluate_condition(conjunction.get(), environment).outcome == ConditionOutcome::Satisfied);

  const auto failing = all_of({min_driver("580.0"), required_os("windows")});
  HCR_CHECK(evaluate_condition(failing.get(), environment).outcome == ConditionOutcome::Unsatisfied);

  const auto disjunction = any_of({min_driver("700.0"), required_os("linux")});
  HCR_CHECK(evaluate_condition(disjunction.get(), environment).outcome == ConditionOutcome::Satisfied);

  const auto unknown_disjunction = any_of({min_driver("700.0"), required_os("linux"), required_privilege("admin")});
  HCR_CHECK(evaluate_condition(unknown_disjunction.get(), environment).outcome == ConditionOutcome::Satisfied);

  const auto unknown_only = any_of({required_privilege("admin"), min_driver("900.0")});
  HCR_CHECK(evaluate_condition(unknown_only.get(), environment).outcome == ConditionOutcome::Unknown);

  auto negated = std::make_shared<ConditionNode>();
  negated->kind = ConditionKind::Not;
  negated->children.push_back(min_driver("580.0"));
  HCR_CHECK(evaluate_condition(negated.get(), environment).outcome == ConditionOutcome::Unsatisfied);

  HCR_CHECK(evaluate_condition(nullptr, environment).outcome == ConditionOutcome::NotApplicable);
}

HCR_TEST(conditions, structure_is_validated) {
  RegistryLimits limits;
  auto logical_without_children = std::make_shared<ConditionNode>();
  logical_without_children->kind = ConditionKind::AllOf;
  HCR_CHECK(!validate_condition(*logical_without_children, limits).ok());

  auto unparseable_minimum = std::make_shared<ConditionNode>();
  unparseable_minimum->kind = ConditionKind::MinDriverVersion;
  unparseable_minimum->value = "not-a-version";
  HCR_CHECK(!validate_condition(*unparseable_minimum, limits).ok());

  auto leaf_with_children = std::make_shared<ConditionNode>();
  leaf_with_children->kind = ConditionKind::RequiredOs;
  leaf_with_children->value = "linux";
  leaf_with_children->children.push_back(min_driver("1.0"));
  HCR_CHECK(!validate_condition(*leaf_with_children, limits).ok());

  auto missing_value = std::make_shared<ConditionNode>();
  missing_value->kind = ConditionKind::RequiredOs;
  HCR_CHECK(!validate_condition(*missing_value, limits).ok());

  auto deep = std::make_shared<ConditionNode>();
  deep->kind = ConditionKind::RequiredOs;
  deep->value = "linux";
  std::shared_ptr<const ConditionNode> current = deep;
  for (int i = 0; i < 12; ++i) {
    auto wrapper = std::make_shared<ConditionNode>();
    wrapper->kind = ConditionKind::AllOf;
    wrapper->children.push_back(current);
    current = wrapper;
  }
  HCR_CHECK(!validate_condition(*current, limits).ok());
}

HCR_TEST(canonical, digests_are_stable_and_order_insensitive_to_input) {
  HCR_CHECK_EQ(fnv1a64("abc"), fnv1a64(std::string_view("abc")));
  HCR_CHECK(fnv1a64("abc") != fnv1a64("abd"));
  HCR_CHECK_EQ(hex64(0), std::string("0000000000000000"));
  HCR_CHECK_EQ(ascii_lower("AbC-1"), std::string("abc-1"));
  HCR_CHECK(equals_ascii_ci("Linux", "linux"));
}
