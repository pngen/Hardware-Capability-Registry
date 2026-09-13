// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/canonical.hpp"

namespace hcr {

std::string ascii_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool equals_ascii_ci(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size, std::uint64_t seed) noexcept {
  std::uint64_t hash = seed;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(data[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t fnv1a64(std::string_view text, std::uint64_t seed) noexcept {
  return fnv1a64(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), seed);
}

void fnv1a64_mix(std::uint64_t& hash, std::string_view text) noexcept {
  hash ^= 0xffu;
  hash *= kFnvPrime;
  hash = fnv1a64(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), hash);
}

void fnv1a64_mix(std::uint64_t& hash, std::uint64_t value) noexcept {
  for (int i = 0; i < 8; ++i) {
    hash ^= static_cast<std::uint64_t>((value >> (i * 8)) & 0xffu);
    hash *= kFnvPrime;
  }
}

std::string hex64(std::uint64_t value) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kDigits[value & 0xfu];
    value >>= 4;
  }
  return out;
}

}  // namespace hcr
