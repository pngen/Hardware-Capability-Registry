// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace hcr {

std::string utc_now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm broken{};
#if defined(_WIN32)
  if (gmtime_s(&broken, &seconds) != 0) return "1970-01-01T00:00:00Z";
#else
  if (gmtime_r(&seconds, &broken) == nullptr) return "1970-01-01T00:00:00Z";
#endif
  char buffer[32];
  const std::size_t written =
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &broken);
  if (written == 0) return "1970-01-01T00:00:00Z";
  return std::string(buffer, written);
}

std::uint64_t monotonic_millis() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace hcr
