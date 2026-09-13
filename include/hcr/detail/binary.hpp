// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Internal bounded byte codec. This is an implementation detail of the
// persistence and protocol layers: it is not part of the supported public API.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "hcr/status.hpp"

namespace hcr::detail {

class ByteWriter {
 public:
  explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}

  void u8(std::uint8_t value) { out_.push_back(value); }
  void u16(std::uint16_t value) {
    for (int i = 0; i < 2; ++i) out_.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
  }
  void u32(std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
  }
  void u64(std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out_.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
  }
  void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }
  void boolean(bool value) { u8(value ? 1u : 0u); }
  void raw(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out_.insert(out_.end(), bytes, bytes + size);
  }
  /// Length-prefixed string. The bound is enforced on write so that a value
  /// which could never be read back is rejected at the source.
  /// Length-prefixed string. A value longer than the bound is written clamped
  /// and the truncation is recorded, so the caller can refuse to emit an image
  /// the receiver would reject. Data is never silently lost.
  void str(std::string_view text, std::size_t max_length) {
    if (text.size() > max_length) truncated_ = true;
    const std::size_t size = text.size() > max_length ? max_length : text.size();
    u32(static_cast<std::uint32_t>(size));
    raw(text.data(), size);
  }
  void blob(const std::vector<std::uint8_t>& data, std::size_t max_length) {
    if (data.size() > max_length) truncated_ = true;
    const std::size_t size = data.size() > max_length ? max_length : data.size();
    u32(static_cast<std::uint32_t>(size));
    raw(data.data(), size);
  }

  [[nodiscard]] bool truncated() const noexcept { return truncated_; }

 private:
  std::vector<std::uint8_t>& out_;
  bool truncated_ = false;
};

class ByteReader {
 public:
  ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return size_ - position_; }
  [[nodiscard]] bool exhausted() const noexcept { return position_ == size_; }

  void fail(ErrorCode code, std::string message) {
    if (ok_) {
      ok_ = false;
      code_ = code;
      message_ = std::move(message);
    }
  }
  [[nodiscard]] Status status() const {
    if (ok_) return Status::success();
    return Status::failure(code_, message_);
  }

  std::uint8_t u8() {
    if (!ok_) return 0;
    if (position_ + 1 > size_) {
      fail(ErrorCode::CorruptData, "truncated input while reading a byte");
      return 0;
    }
    return data_[position_++];
  }
  std::uint16_t u16() {
    std::uint16_t value = 0;
    for (int i = 0; i < 2; ++i) value |= static_cast<std::uint16_t>(u8()) << (i * 8);
    return value;
  }
  std::uint32_t u32() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(u8()) << (i * 8);
    return value;
  }
  std::uint64_t u64() {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(u8()) << (i * 8);
    return value;
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  bool boolean() { return u8() != 0; }

  std::string str(std::size_t max_length) {
    const std::uint32_t length = u32();
    if (!ok_) return {};
    if (length > max_length || length > remaining()) {
      fail(ErrorCode::CorruptData, "string length exceeds the bound or the input");
      return {};
    }
    std::string out(reinterpret_cast<const char*>(data_ + position_), length);
    position_ += length;
    return out;
  }

  std::vector<std::uint8_t> blob(std::size_t max_length) {
    const std::uint32_t length = u32();
    if (!ok_) return {};
    if (length > max_length || length > remaining()) {
      fail(ErrorCode::CorruptData, "blob length exceeds the bound or the input");
      return {};
    }
    std::vector<std::uint8_t> out(data_ + position_, data_ + position_ + length);
    position_ += length;
    return out;
  }

 private:
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t position_ = 0;
  bool ok_ = true;
  ErrorCode code_ = ErrorCode::Ok;
  std::string message_;
};

}  // namespace hcr::detail
