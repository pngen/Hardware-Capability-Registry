// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/ids.hpp"

namespace hcr {

bool is_canonical_token(std::string_view text) noexcept {
  if (text.empty() || text.size() > kMaxTokenLength) return false;
  const char first = text.front();
  const bool first_alnum = (first >= 'a' && first <= 'z') || (first >= '0' && first <= '9');
  if (!first_alnum) return false;
  for (const char c : text) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) continue;
    if (c == '.' || c == '_' || c == ':' || c == '-') continue;
    return false;
  }
  return true;
}

}  // namespace hcr
