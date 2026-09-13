// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hcr {

/// Dotted numeric software version (driver, firmware, runtime, kernel).
///
/// Parsing is deliberately forgiving about textual suffixes ("580.65.06",
/// "12.9.86", "6.14.0-24-generic", "1.2.3+build7") because vendor version
/// strings are not uniform. Comparison is numeric component-wise over the
/// leading numeric run; a shorter version is zero-padded. A missing component
/// therefore never sorts ahead of the same prefix with a trailing non-zero
/// component. Textual remainder is preserved for explanation only and never
/// participates in ordering.
class SoftwareVersion {
 public:
  SoftwareVersion() = default;

  static SoftwareVersion parse(std::string_view text);
  static bool is_parseable(std::string_view text) noexcept;

  [[nodiscard]] bool valid() const noexcept { return !raw_.empty(); }
  [[nodiscard]] const std::string& raw() const noexcept { return raw_; }
  [[nodiscard]] const std::vector<std::uint64_t>& components() const noexcept { return components_; }

  /// Deterministic three-way comparison. Returns -1, 0 or 1.
  static int compare(const SoftwareVersion& a, const SoftwareVersion& b) noexcept;

  friend bool operator==(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) == 0;
  }
  friend bool operator!=(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) != 0;
  }
  friend bool operator<(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) < 0;
  }
  friend bool operator<=(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) <= 0;
  }
  friend bool operator>(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) > 0;
  }
  friend bool operator>=(const SoftwareVersion& a, const SoftwareVersion& b) noexcept {
    return compare(a, b) >= 0;
  }

 private:
  std::string raw_;
  std::vector<std::uint64_t> components_;
};

/// Inclusive-exclusive version interval. An unset bound means unbounded.
struct VersionRange {
  SoftwareVersion minimum;
  SoftwareVersion maximum;
  bool include_maximum = false;

  [[nodiscard]] bool valid() const noexcept {
    if (!minimum.valid() || !maximum.valid()) return true;
    const int cmp = SoftwareVersion::compare(minimum, maximum);
    if (cmp > 0) return false;
    if (cmp == 0) return include_maximum;
    return true;
  }

  [[nodiscard]] bool contains(const SoftwareVersion& v) const noexcept {
    if (!v.valid()) return false;
    if (minimum.valid() && SoftwareVersion::compare(v, minimum) < 0) return false;
    if (maximum.valid()) {
      const int cmp = SoftwareVersion::compare(v, maximum);
      if (cmp > 0) return false;
      if (cmp == 0 && !include_maximum) return false;
    }
    return true;
  }

  /// Renders deterministically, e.g. "[1.0,2.0)" or ">=3.1".
  std::string describe() const;
};

}  // namespace hcr
