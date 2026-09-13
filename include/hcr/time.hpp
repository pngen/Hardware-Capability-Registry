// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace hcr {

/// UTC timestamp with seconds precision, ISO-8601 ("2026-01-31T12:00:00Z").
/// Observation times are provenance metadata only: they never participate in
/// authority ordering, which is why second precision is sufficient and why
/// query results stay deterministic for identical state.
std::string utc_now_iso8601();

/// Monotonic millisecond clock. Used for latency measurement and for
/// deterministic ordering of observations from one publisher, never for
/// authority.
std::uint64_t monotonic_millis();

}  // namespace hcr
