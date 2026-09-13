// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/condition.hpp"

#include "hcr/canonical.hpp"

namespace hcr {
namespace {

bool is_comparison_kind(ConditionKind kind) noexcept {
  switch (kind) {
    case ConditionKind::MinDriverVersion:
    case ConditionKind::MinFirmwareVersion:
    case ConditionKind::MinRuntimeVersion:
    case ConditionKind::MinPlatformGeneration:
    case ConditionKind::RequiredKernelVersion:
      return true;
    default:
      return false;
  }
}

bool is_equality_kind(ConditionKind kind) noexcept {
  switch (kind) {
    case ConditionKind::RequiredOs:
    case ConditionKind::RequiredDeviceMode:
    case ConditionKind::RequiredTopology:
    case ConditionKind::RequiredPeerHardware:
    case ConditionKind::RequiredPartitionState:
    case ConditionKind::RequiredPrivilege:
    case ConditionKind::RequiredDeviceGeneration:
      return true;
    default:
      return false;
  }
}

const char* comparison_operator_text(ConditionKind kind) noexcept {
  switch (kind) {
    case ConditionKind::MinPlatformGeneration:
    case ConditionKind::RequiredDeviceGeneration:
      return ">=";
    default:
      return ">=";
  }
}

}  // namespace

const char* to_string(ConditionKind kind) noexcept {
  switch (kind) {
    case ConditionKind::Always: return "always";
    case ConditionKind::MinDriverVersion: return "min-driver-version";
    case ConditionKind::MinFirmwareVersion: return "min-firmware-version";
    case ConditionKind::MinRuntimeVersion: return "min-runtime-version";
    case ConditionKind::MinPlatformGeneration: return "min-platform-generation";
    case ConditionKind::RequiredOs: return "required-os";
    case ConditionKind::RequiredKernelVersion: return "required-kernel-version";
    case ConditionKind::RequiredDeviceMode: return "required-device-mode";
    case ConditionKind::RequiredTopology: return "required-topology";
    case ConditionKind::RequiredPeerHardware: return "required-peer-hardware";
    case ConditionKind::RequiredPartitionState: return "required-partition-state";
    case ConditionKind::RequiredPlatformSetting: return "required-platform-setting";
    case ConditionKind::RequiredPrivilege: return "required-privilege";
    case ConditionKind::RequiredDeviceGeneration: return "required-device-generation";
    case ConditionKind::RequiredFeatureGate: return "required-feature-gate";
    case ConditionKind::AllOf: return "all-of";
    case ConditionKind::AnyOf: return "any-of";
    case ConditionKind::Not: return "not";
  }
  return "always";
}

bool condition_kind_from_string(std::string_view text, ConditionKind& out) noexcept {
  for (int i = 0; i <= static_cast<int>(ConditionKind::Not); ++i) {
    const auto kind = static_cast<ConditionKind>(i);
    if (equals_ascii_ci(text, to_string(kind))) {
      out = kind;
      return true;
    }
  }
  return false;
}

const char* to_string(ConditionOutcome outcome) noexcept {
  switch (outcome) {
    case ConditionOutcome::Satisfied: return "satisfied";
    case ConditionOutcome::Unsatisfied: return "unsatisfied";
    case ConditionOutcome::Unknown: return "unknown";
    case ConditionOutcome::NotApplicable: return "not-applicable";
  }
  return "unknown";
}

void EnvironmentContext::set(std::string key, std::string value) {
  facts_[ascii_lower(key)] = ascii_lower(value);
}

bool EnvironmentContext::has(std::string_view key) const {
  return facts_.find(std::string(key)) != facts_.end();
}

const std::string* EnvironmentContext::find(std::string_view key) const {
  const auto it = facts_.find(std::string(key));
  if (it == facts_.end()) return nullptr;
  return &it->second;
}

Status validate_condition(const ConditionNode& node, const RegistryLimits& limits) {
  struct Frame {
    const ConditionNode* node;
    std::size_t depth;
  };
  std::vector<Frame> stack;
  stack.push_back({&node, 1});
  std::size_t visited = 0;
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();
    ++visited;
    if (visited > limits.max_condition_nodes) {
      return Status::failure(ErrorCode::LimitExceeded, "condition tree exceeds node limit");
    }
    if (frame.depth > limits.max_record_depth + 2) {
      return Status::failure(ErrorCode::LimitExceeded, "condition tree exceeds depth limit");
    }
    if (frame.node == nullptr) {
      return Status::failure(ErrorCode::InvalidCondition, "null condition node");
    }
    const ConditionNode& current = *frame.node;
    if (current.children.size() > limits.max_condition_children) {
      return Status::failure(ErrorCode::LimitExceeded, "condition node exceeds child limit");
    }
    if (current.key.size() > limits.max_name_length || current.value.size() > limits.max_name_length) {
      return Status::failure(ErrorCode::LimitExceeded, "condition key or value exceeds length limit");
    }
    switch (current.kind) {
      case ConditionKind::AllOf:
      case ConditionKind::AnyOf:
        if (current.children.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "logical condition requires children");
        }
        if (!current.key.empty() || !current.value.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "logical condition must not carry key or value");
        }
        break;
      case ConditionKind::Not:
        if (current.children.size() != 1) {
          return Status::failure(ErrorCode::InvalidCondition, "not requires exactly one child");
        }
        break;
      case ConditionKind::Always:
        if (!current.children.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "always must not have children");
        }
        break;
      case ConditionKind::RequiredFeatureGate:
        if (current.key.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "feature gate condition requires a gate name");
        }
        break;
      case ConditionKind::RequiredPlatformSetting:
        if (current.key.empty() || current.value.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "platform setting condition requires name and value");
        }
        break;
      default:
        if (current.value.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "condition requires a value");
        }
        if (is_comparison_kind(current.kind) && !SoftwareVersion::is_parseable(current.value)) {
          return Status::failure(ErrorCode::InvalidCondition, "version comparison requires a parseable version");
        }
        if (is_equality_kind(current.kind) && !current.children.empty()) {
          return Status::failure(ErrorCode::InvalidCondition, "leaf condition must not have children");
        }
        break;
    }
    for (const auto& child : current.children) {
      stack.push_back({child.get(), frame.depth + 1});
    }
  }
  if (!is_comparison_kind(node.kind) && !is_equality_kind(node.kind) && node.kind != ConditionKind::Always &&
      node.kind != ConditionKind::AllOf && node.kind != ConditionKind::AnyOf && node.kind != ConditionKind::Not &&
      node.kind != ConditionKind::RequiredFeatureGate && node.kind != ConditionKind::RequiredPlatformSetting) {
    return Status::failure(ErrorCode::InvalidCondition, "unknown condition kind");
  }
  return Status::success();
}

std::string describe_condition(const ConditionNode* node) {
  if (node == nullptr) return "always";
  switch (node->kind) {
    case ConditionKind::Always:
      return "always";
    case ConditionKind::AllOf:
    case ConditionKind::AnyOf: {
      std::string out = node->kind == ConditionKind::AllOf ? "all_of(" : "any_of(";
      for (std::size_t i = 0; i < node->children.size(); ++i) {
        if (i != 0) out += ", ";
        out += describe_condition(node->children[i].get());
      }
      out += ")";
      return out;
    }
    case ConditionKind::Not:
      return std::string("not(") + describe_condition(node->children.front().get()) + ")";
    case ConditionKind::MinDriverVersion:
      return std::string(env_keys::kDriverVersion) + ">=" + node->value;
    case ConditionKind::MinFirmwareVersion:
      return std::string(env_keys::kFirmwareVersion) + ">=" + node->value;
    case ConditionKind::MinRuntimeVersion:
      return std::string(env_keys::kRuntimeVersion) + ">=" + node->value;
    case ConditionKind::MinPlatformGeneration:
      return std::string(env_keys::kPlatformGeneration) + comparison_operator_text(node->kind) + node->value;
    case ConditionKind::RequiredOs:
      return std::string(env_keys::kOs) + "==" + node->value;
    case ConditionKind::RequiredKernelVersion:
      return std::string(env_keys::kKernelVersion) + ">=" + node->value;
    case ConditionKind::RequiredDeviceMode:
      return std::string(env_keys::kDeviceMode) + "==" + node->value;
    case ConditionKind::RequiredTopology:
      return std::string(env_keys::kTopology) + "==" + node->value;
    case ConditionKind::RequiredPeerHardware:
      return std::string(env_keys::kPeerHardware) + "==" + node->value;
    case ConditionKind::RequiredPartitionState:
      return std::string(env_keys::kPartitionState) + "==" + node->value;
    case ConditionKind::RequiredPrivilege:
      return std::string(env_keys::kPrivilege) + "==" + node->value;
    case ConditionKind::RequiredDeviceGeneration:
      return std::string(env_keys::kDeviceGeneration) + comparison_operator_text(node->kind) + node->value;
    case ConditionKind::RequiredPlatformSetting:
      return std::string(env_keys::kPlatformSettingPrefix) + node->key + "==" + node->value;
    case ConditionKind::RequiredFeatureGate:
      return std::string(env_keys::kFeatureGatePrefix) + node->key + "==present";
  }
  return "always";
}

ConditionEvaluation evaluate_condition(const ConditionNode* node, const EnvironmentContext& env) {
  ConditionEvaluation result;
  if (node == nullptr) {
    result.outcome = ConditionOutcome::NotApplicable;
    return result;
  }

  const auto record = [&result](std::string expression, ConditionOutcome outcome, std::string detail) {
    result.evidence.push_back(ConditionEvidence{std::move(expression), outcome, std::move(detail)});
  };

  switch (node->kind) {
    case ConditionKind::Always:
      result.outcome = ConditionOutcome::Satisfied;
      record("always", ConditionOutcome::Satisfied, "unconditional");
      return result;
    case ConditionKind::AllOf: {
      bool any_unknown = false;
      bool any_unsatisfied = false;
      for (const auto& child : node->children) {
        ConditionEvaluation evaluation = evaluate_condition(child.get(), env);
        result.evidence.insert(result.evidence.end(), evaluation.evidence.begin(), evaluation.evidence.end());
        if (evaluation.outcome == ConditionOutcome::Unsatisfied) any_unsatisfied = true;
        else if (evaluation.outcome == ConditionOutcome::Unknown) any_unknown = true;
      }
      if (any_unsatisfied) result.outcome = ConditionOutcome::Unsatisfied;
      else if (any_unknown) result.outcome = ConditionOutcome::Unknown;
      else result.outcome = ConditionOutcome::Satisfied;
      return result;
    }
    case ConditionKind::AnyOf: {
      bool any_unknown = false;
      bool any_satisfied = false;
      for (const auto& child : node->children) {
        ConditionEvaluation evaluation = evaluate_condition(child.get(), env);
        result.evidence.insert(result.evidence.end(), evaluation.evidence.begin(), evaluation.evidence.end());
        if (evaluation.outcome == ConditionOutcome::Satisfied) any_satisfied = true;
        else if (evaluation.outcome == ConditionOutcome::Unknown) any_unknown = true;
      }
      if (any_satisfied) result.outcome = ConditionOutcome::Satisfied;
      else if (any_unknown) result.outcome = ConditionOutcome::Unknown;
      else result.outcome = ConditionOutcome::Unsatisfied;
      return result;
    }
    case ConditionKind::Not: {
      ConditionEvaluation evaluation = evaluate_condition(node->children.front().get(), env);
      result.evidence = evaluation.evidence;
      switch (evaluation.outcome) {
        case ConditionOutcome::Satisfied: result.outcome = ConditionOutcome::Unsatisfied; break;
        case ConditionOutcome::Unsatisfied: result.outcome = ConditionOutcome::Satisfied; break;
        case ConditionOutcome::Unknown: result.outcome = ConditionOutcome::Unknown; break;
        case ConditionOutcome::NotApplicable: result.outcome = ConditionOutcome::NotApplicable; break;
      }
      return result;
    }
    default:
      break;
  }

  const auto evaluate_version_minimum = [&](const char* key) {
    const std::string* observed = env.find(key);
    if (observed == nullptr) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, std::string("no observed ") + key);
      return;
    }
    const SoftwareVersion required = SoftwareVersion::parse(node->value);
    const SoftwareVersion actual = SoftwareVersion::parse(*observed);
    if (!required.valid() || !actual.valid()) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, "unparseable version");
      return;
    }
    if (SoftwareVersion::compare(actual, required) >= 0) {
      result.outcome = ConditionOutcome::Satisfied;
      record(describe_condition(node), ConditionOutcome::Satisfied, std::string(key) + "=" + actual.raw());
    } else {
      result.outcome = ConditionOutcome::Unsatisfied;
      record(describe_condition(node), ConditionOutcome::Unsatisfied,
             std::string(key) + "=" + actual.raw() + " < required " + required.raw());
    }
  };

  const auto evaluate_equality = [&](const char* key) {
    const std::string* observed = env.find(key);
    if (observed == nullptr) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, std::string("no observed ") + key);
      return;
    }
    if (equals_ascii_ci(*observed, node->value)) {
      result.outcome = ConditionOutcome::Satisfied;
      record(describe_condition(node), ConditionOutcome::Satisfied, std::string(key) + "=" + *observed);
    } else {
      result.outcome = ConditionOutcome::Unsatisfied;
      record(describe_condition(node), ConditionOutcome::Unsatisfied,
             std::string(key) + "=" + *observed + " != required " + node->value);
    }
  };

  const auto evaluate_generation_minimum = [&](const char* key) {
    const std::string* observed = env.find(key);
    if (observed == nullptr) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, std::string("no observed ") + key);
      return;
    }
    std::uint64_t required = 0;
    std::uint64_t actual = 0;
    try {
      required = static_cast<std::uint64_t>(std::stoull(node->value));
    } catch (...) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, "unparseable generation requirement");
      return;
    }
    try {
      actual = static_cast<std::uint64_t>(std::stoull(*observed));
    } catch (...) {
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, "unparseable observed generation");
      return;
    }
    if (actual >= required) {
      result.outcome = ConditionOutcome::Satisfied;
      record(describe_condition(node), ConditionOutcome::Satisfied,
             std::string(key) + "=" + std::to_string(actual));
    } else {
      result.outcome = ConditionOutcome::Unsatisfied;
      record(describe_condition(node), ConditionOutcome::Unsatisfied,
             std::string(key) + "=" + std::to_string(actual) + " < required " + node->value);
    }
  };

  switch (node->kind) {
    case ConditionKind::MinDriverVersion: evaluate_version_minimum(env_keys::kDriverVersion); break;
    case ConditionKind::MinFirmwareVersion: evaluate_version_minimum(env_keys::kFirmwareVersion); break;
    case ConditionKind::MinRuntimeVersion: evaluate_version_minimum(env_keys::kRuntimeVersion); break;
    case ConditionKind::RequiredKernelVersion: evaluate_version_minimum(env_keys::kKernelVersion); break;
    case ConditionKind::MinPlatformGeneration: evaluate_generation_minimum(env_keys::kPlatformGeneration); break;
    case ConditionKind::RequiredDeviceGeneration: evaluate_generation_minimum(env_keys::kDeviceGeneration); break;
    case ConditionKind::RequiredOs: evaluate_equality(env_keys::kOs); break;
    case ConditionKind::RequiredDeviceMode: evaluate_equality(env_keys::kDeviceMode); break;
    case ConditionKind::RequiredTopology: evaluate_equality(env_keys::kTopology); break;
    case ConditionKind::RequiredPeerHardware: evaluate_equality(env_keys::kPeerHardware); break;
    case ConditionKind::RequiredPartitionState: evaluate_equality(env_keys::kPartitionState); break;
    case ConditionKind::RequiredPrivilege: evaluate_equality(env_keys::kPrivilege); break;
    case ConditionKind::RequiredPlatformSetting: {
      const std::string full_key = std::string(env_keys::kPlatformSettingPrefix) + node->key;
      const std::string* observed = env.find(full_key);
      if (observed == nullptr) {
        result.outcome = ConditionOutcome::Unknown;
        record(describe_condition(node), ConditionOutcome::Unknown, "platform setting not supplied");
      } else if (equals_ascii_ci(*observed, node->value)) {
        result.outcome = ConditionOutcome::Satisfied;
        record(describe_condition(node), ConditionOutcome::Satisfied, full_key + "=" + *observed);
      } else {
        result.outcome = ConditionOutcome::Unsatisfied;
        record(describe_condition(node), ConditionOutcome::Unsatisfied,
               full_key + "=" + *observed + " != required " + node->value);
      }
      break;
    }
    case ConditionKind::RequiredFeatureGate: {
      const std::string full_key = std::string(env_keys::kFeatureGatePrefix) + node->key;
      if (env.has(full_key)) {
        result.outcome = ConditionOutcome::Satisfied;
        record(describe_condition(node), ConditionOutcome::Satisfied, full_key + " present");
      } else {
        result.outcome = ConditionOutcome::Unsatisfied;
        record(describe_condition(node), ConditionOutcome::Unsatisfied, full_key + " absent");
      }
      break;
    }
    default:
      result.outcome = ConditionOutcome::Unknown;
      record(describe_condition(node), ConditionOutcome::Unknown, "unsupported condition kind");
      break;
  }
  return result;
}

}  // namespace hcr
