// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hcr {

/// ASCII lowercase folding. The registry compares tokens byte-wise; folding is
/// applied only where a human- or vendor-supplied string must be normalized
/// before it becomes a canonical token.
std::string ascii_lower(std::string_view text);

/// Byte-wise, locale-independent ASCII case-insensitive comparison.
bool equals_ascii_ci(std::string_view a, std::string_view b) noexcept;

/// FNV-1a 64-bit hash. Used for canonical digests; it is a determinism and
/// change-detection aid, not a cryptographic guarantee.
inline constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size, std::uint64_t seed = kFnvOffsetBasis) noexcept;
std::uint64_t fnv1a64(std::string_view text, std::uint64_t seed = kFnvOffsetBasis) noexcept;
void fnv1a64_mix(std::uint64_t& hash, std::string_view text) noexcept;
void fnv1a64_mix(std::uint64_t& hash, std::uint64_t value) noexcept;

/// Canonical rendering of a 64-bit digest as 16 lowercase hex digits.
std::string hex64(std::uint64_t value);

}  // namespace hcr
