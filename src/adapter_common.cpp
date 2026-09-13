// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "adapter_common.hpp"

#include <memory>

namespace hcr::adapter_detail {

std::string canonical_token(std::string_view text) {
  std::string out;
  out.reserve(text.size() < kMaxTokenLength ? text.size() : kMaxTokenLength);
  bool previous_separator = true;
  for (const char raw : text) {
    if (out.size() >= kMaxTokenLength) break;
    char c = raw;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      out.push_back(c);
      previous_separator = false;
      continue;
    }
    if (c == '.' || c == '_' || c == ':' || c == '-') {
      if (!previous_separator) {
        out.push_back('.');
        previous_separator = true;
      }
      continue;
    }
    if (!previous_separator) {
      out.push_back('.');
      previous_separator = true;
    }
  }
  while (!out.empty() && out.back() == '.') out.pop_back();
  std::size_t start = 0;
  while (start < out.size() && out[start] == '.') ++start;
  if (start != 0) out.erase(0, start);
  if (out.empty()) out = "unnamed";
  return out;
}

std::string token_from_hash(std::string_view prefix, std::string_view text) {
  std::uint64_t hash = kFnvOffsetBasis;
  fnv1a64_mix(hash, text);
  return std::string(prefix) + "." + hex64(hash);
}

std::string hex_bytes(const void* data, std::size_t size) {
  static const char kDigits[] = "0123456789abcdef";
  const auto* bytes_value = static_cast<const std::uint8_t*>(data);
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(kDigits[(bytes_value[i] >> 4) & 0xfu]);
    out.push_back(kDigits[bytes_value[i] & 0xfu]);
  }
  return out;
}

std::string format_gpu_uuid(const std::uint8_t* bytes_value) {
  const std::string hex = hex_bytes(bytes_value, 16);
  std::string out = "GPU-";
  out += hex.substr(0, 8);
  out += "-";
  out += hex.substr(8, 4);
  out += "-";
  out += hex.substr(12, 4);
  out += "-";
  out += hex.substr(16, 4);
  out += "-";
  out += hex.substr(20, 12);
  return out;
}

ConditionPtr condition(ConditionKind kind, std::string key, std::string value) {
  return std::make_shared<const ConditionNode>(kind, std::move(key), std::move(value));
}

ConditionPtr all_of(std::vector<ConditionPtr> children) {
  auto node = std::make_shared<ConditionNode>();
  node->kind = ConditionKind::AllOf;
  for (auto& child : children) {
    if (child != nullptr) node->children.push_back(std::move(child));
  }
  if (node->children.size() == 1) return node->children.front();
  return node;
}

}  // namespace hcr::adapter_detail
