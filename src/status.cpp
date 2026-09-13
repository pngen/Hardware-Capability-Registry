// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/status.hpp"

namespace hcr {

const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::InvalidIdentity: return "invalid-identity";
    case ErrorCode::InvalidCapability: return "invalid-capability";
    case ErrorCode::InvalidValue: return "invalid-value";
    case ErrorCode::InvalidSupportState: return "invalid-support-state";
    case ErrorCode::InvalidProvenance: return "invalid-provenance";
    case ErrorCode::InvalidCondition: return "invalid-condition";
    case ErrorCode::InvalidRange: return "invalid-range";
    case ErrorCode::InvalidReference: return "invalid-reference";
    case ErrorCode::LimitExceeded: return "limit-exceeded";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::AlreadyExists: return "already-exists";
    case ErrorCode::StaleEpoch: return "stale-epoch";
    case ErrorCode::StaleBoot: return "stale-boot";
    case ErrorCode::StaleDeviceGeneration: return "stale-device-generation";
    case ErrorCode::StaleCapabilityGeneration: return "stale-capability-generation";
    case ErrorCode::StaleSequence: return "stale-sequence";
    case ErrorCode::GenerationRegression: return "generation-regression";
    case ErrorCode::DuplicateConflict: return "duplicate-conflict";
    case ErrorCode::FencedPublisher: return "fenced-publisher";
    case ErrorCode::UnknownPublisher: return "unknown-publisher";
    case ErrorCode::UnsupportedVersion: return "unsupported-version";
    case ErrorCode::CorruptData: return "corrupt-data";
    case ErrorCode::IntegrityFailure: return "integrity-failure";
    case ErrorCode::IoFailure: return "io-failure";
    case ErrorCode::InvariantViolation: return "invariant-violation";
    case ErrorCode::QueueFull: return "queue-full";
    case ErrorCode::ShuttingDown: return "shutting-down";
    case ErrorCode::ProtocolError: return "protocol-error";
    case ErrorCode::Internal: return "internal";
  }
  return "unknown";
}

std::string Status::describe() const {
  if (ok()) return "ok";
  std::string out = to_string(code_);
  if (!message_.empty()) {
    out += ": ";
    out += message_;
  }
  return out;
}

}  // namespace hcr
