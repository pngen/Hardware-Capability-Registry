// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hcr/durable_state.hpp"
#include "hcr/limits.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// A persisted container is validated completely before any of it is applied.
/// A failed load never leaves partially applied state behind.
struct PersistOptions {
  RegistryLimits limits;
  /// Refuse to write more than this many bytes.
  std::size_t max_bytes = 0;
};

/// Canonical binary encoding of durable state.
///
/// Layout: magic, format version, flags, header, then length-prefixed,
/// checksum-protected sections in canonical order. Counts are checked against
/// limits before allocation; every string is length-bounded; every generation
/// is validated; the whole image is protected by a CRC-32 integrity check.
Status encode_durable_state(const DurableState& state, const PersistOptions& options, std::vector<std::uint8_t>& out);
Status decode_durable_state(const std::vector<std::uint8_t>& bytes, const PersistOptions& options, DurableState& out);

/// Persistence boundary. Implementations must write atomically: readers never
/// observe a partially written file.
class StateStore {
 public:
  virtual ~StateStore();

  /// Writes. Implementations must either replace the target completely or
  /// leave the previous contents intact.
  virtual Status save(const DurableState& state) = 0;

  /// Reads and validates. A failure leaves the caller's registry untouched.
  virtual Status load(DurableState& out) = 0;

  /// True when this store already holds an image. A missing image is a first
  /// start, not an error; an unreadable one is still an error.
  virtual bool exists() const = 0;

  virtual std::string describe() const = 0;
};

/// File-backed store with atomic replacement through a temporary file in the
/// same directory.
class FileStateStore final : public StateStore {
 public:
  explicit FileStateStore(std::string path, PersistOptions options = PersistOptions{});
  ~FileStateStore() override;

  Status save(const DurableState& state) override;
  Status load(DurableState& out) override;
  bool exists() const override;
  std::string describe() const override;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] const PersistOptions& options() const noexcept { return options_; }

 private:
  std::string path_;
  PersistOptions options_;
};

}  // namespace hcr
