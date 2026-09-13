// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "hcr/capability.hpp"

namespace hcr {

/// Built-in capability catalog.
///
/// The catalog declares the typed, versioned schemas the registry understands
/// out of the box. Publishers must reference a known schema; a curated or
/// imported manifest may register additional schemas, but every capability
/// always has a declared value kind. The catalog makes no claim about any
/// device: it declares types, never support.
const std::vector<CapabilitySchema>& builtin_capability_schemas();

/// Looks up a built-in schema by schema identity or by capability identity.
const CapabilitySchema* find_builtin_schema_by_capability(const CapabilityId& capability);
const CapabilitySchema* find_builtin_schema(const CapabilitySchemaId& schema);

}  // namespace hcr
