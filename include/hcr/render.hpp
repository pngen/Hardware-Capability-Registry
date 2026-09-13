// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "hcr/durable_state.hpp"
#include "hcr/registry.hpp"
#include "hcr/resolution.hpp"
#include "hcr/snapshot.hpp"

namespace hcr {

/// Deterministic text rendering used by the CLI, by examples and by query
/// explanations. Identical registry state always renders identically: every
/// list is emitted in canonical order and no map iteration order leaks in.
std::string render_capability(const ResolvedCapability& value);
std::string render_capability_brief(const ResolvedCapability& value);
std::string render_subject(const SubjectSnapshot& subject);
std::string render_subject_brief(const SubjectSnapshot& subject);
std::string render_quirk(const QuirkRecord& quirk);
std::string render_quirk_application(const QuirkApplication& application);
std::string render_compatibility(const CompatibilityFact& fact);
std::string render_publisher(const PublisherRecord& publisher);
std::string render_snapshot_summary(const RegistrySnapshot& snapshot);
std::string render_comparison(const DeviceComparison& comparison);
std::string render_resource_usage(const ResourceUsage& usage);
std::string render_import_report(const ImportReport& report);

/// One-line classification token: "REAL", "SYNTHETIC", "UNSUPPORTED" or
/// "UNKNOWN".
std::string render_truth_class(TruthClass value);

/// Deterministic digest over a snapshot's canonical serialization.
std::uint64_t canonical_snapshot_digest(const RegistrySnapshot& snapshot);

/// Deterministic digest over durable state. Used by the persistence layer to
/// detect corruption and by import to reject files whose contents changed.
std::uint64_t canonical_durable_digest(const DurableState& state);

}  // namespace hcr
