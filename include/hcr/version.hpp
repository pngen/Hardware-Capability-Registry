// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define HCR_VERSION_MAJOR 1
#define HCR_VERSION_MINOR 0
#define HCR_VERSION_PATCH 0
#define HCR_VERSION_STRING "1.0.0"

namespace hcr {

/// Semantic version of this build of the registry runtime.
inline constexpr const char* version_string() noexcept { return HCR_VERSION_STRING; }

/// Major version of the on-the-wire protocol implemented by this build.
inline constexpr unsigned protocol_version() noexcept { return 1u; }

/// Major version of the persisted state format implemented by this build.
inline constexpr unsigned state_format_version() noexcept { return 1u; }

}  // namespace hcr
