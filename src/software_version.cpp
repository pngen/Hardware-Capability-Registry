// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/software_version.hpp"

#include <cstddef>
#include <limits>

namespace hcr {
namespace {

constexpr std::size_t kMaxComponents = 8;

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

}  // namespace

bool SoftwareVersion::is_parseable(std::string_view text) noexcept {
  if (text.empty() || text.size() > 128) return false;
  for (const char c : text) {
    if (is_digit(c)) return true;
  }
  return false;
}

SoftwareVersion SoftwareVersion::parse(std::string_view text) {
  SoftwareVersion version;
  if (text.empty() || text.size() > 128) return version;

  std::vector<std::uint64_t> components;
  std::size_t index = 0;
  while (index < text.size() && components.size() < kMaxComponents) {
    if (!is_digit(text[index])) {
      ++index;
      continue;
    }
    std::uint64_t value = 0;
    bool overflow = false;
    while (index < text.size() && is_digit(text[index])) {
      const std::uint64_t digit = static_cast<std::uint64_t>(text[index] - '0');
      if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ull) {
        overflow = true;
        break;
      }
      value = value * 10ull + digit;
      ++index;
    }
    if (overflow) {
      // A component too large to represent is treated as saturated; ordering
      // stays total and deterministic.
      value = std::numeric_limits<std::uint64_t>::max();
      while (index < text.size() && is_digit(text[index])) ++index;
    }
    components.push_back(value);
  }

  if (components.empty()) return version;
  version.raw_.assign(text);
  version.components_ = std::move(components);
  return version;
}

int SoftwareVersion::compare(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
  if (!a.valid() && !b.valid()) return 0;
  if (!a.valid()) return -1;
  if (!b.valid()) return 1;
  const std::size_t count = a.components_.size() > b.components_.size() ? a.components_.size() : b.components_.size();
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint64_t left = i < a.components_.size() ? a.components_[i] : 0ull;
    const std::uint64_t right = i < b.components_.size() ? b.components_[i] : 0ull;
    if (left < right) return -1;
    if (left > right) return 1;
  }
  return 0;
}

std::string VersionRange::describe() const {
  const bool has_min = minimum.valid();
  const bool has_max = maximum.valid();
  if (has_min && has_max) {
    return std::string("[") + minimum.raw() + "," + maximum.raw() + (include_maximum ? "]" : ")");
  }
  if (has_min) return std::string(">=") + minimum.raw();
  if (has_max) return std::string(include_maximum ? "<=" : "<") + maximum.raw();
  return "*";
}

}  // namespace hcr
