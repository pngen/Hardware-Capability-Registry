// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <utility>

namespace hcr {

/// Stable error taxonomy. Codes are part of the public contract: downstream
/// infrastructure branches on them, so values must not be renumbered.
enum class ErrorCode {
  Ok = 0,
  InvalidArgument,
  InvalidIdentity,
  InvalidCapability,
  InvalidValue,
  InvalidSupportState,
  InvalidProvenance,
  InvalidCondition,
  InvalidRange,
  InvalidReference,
  LimitExceeded,
  NotFound,
  AlreadyExists,
  StaleEpoch,
  StaleBoot,
  StaleDeviceGeneration,
  StaleCapabilityGeneration,
  StaleSequence,
  GenerationRegression,
  DuplicateConflict,
  FencedPublisher,
  UnknownPublisher,
  UnsupportedVersion,
  CorruptData,
  IntegrityFailure,
  IoFailure,
  InvariantViolation,
  QueueFull,
  ShuttingDown,
  ProtocolError,
  Internal,
};

const char* to_string(ErrorCode code) noexcept;

/// A value-or-error carrier used by every fallible public API.
class Status {
 public:
  Status() noexcept = default;

  /// Success factory. Named distinctly from the ok() predicate so that a
  /// Status can never be confused with its own construction.
  static Status success() noexcept { return Status{}; }
  static Status failure(ErrorCode code, std::string message) {
    Status s;
    s.code_ = code;
    s.message_ = std::move(message);
    return s;
  }

  [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::Ok; }
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  explicit operator bool() const noexcept { return ok(); }

  /// Deterministic single-line rendering: "ok" or "<code>: <message>".
  std::string describe() const;

 private:
  ErrorCode code_ = ErrorCode::Ok;
  std::string message_;
};

/// Helper mirroring Status for value-returning APIs.
template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}
  Result(Status status) : status_(std::move(status)) {}

  [[nodiscard]] bool ok() const noexcept { return status_.ok(); }
  [[nodiscard]] const Status& status() const noexcept { return status_; }
  [[nodiscard]] const T& value() const { return value_; }
  [[nodiscard]] T& value() { return value_; }

 private:
  T value_{};
  Status status_{};
};

}  // namespace hcr
