// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hcr/limits.hpp"
#include "hcr/software_version.hpp"
#include "hcr/status.hpp"

namespace hcr {

/// Leaf predicate kinds. Each key maps to one canonical fact name in an
/// EnvironmentContext, so evaluation never depends on locale or ordering.
enum class ConditionKind {
  Always,
  MinDriverVersion,
  MinFirmwareVersion,
  MinRuntimeVersion,
  MinPlatformGeneration,
  RequiredOs,
  RequiredKernelVersion,
  RequiredDeviceMode,
  RequiredTopology,
  RequiredPeerHardware,
  RequiredPartitionState,
  RequiredPlatformSetting,
  RequiredPrivilege,
  RequiredDeviceGeneration,
  RequiredFeatureGate,
  AllOf,
  AnyOf,
  Not,
};

const char* to_string(ConditionKind kind) noexcept;
bool condition_kind_from_string(std::string_view text, ConditionKind& out) noexcept;

/// Canonical fact names consulted by leaf predicates.
namespace env_keys {
inline constexpr const char* kDriverVersion = "driver.version";
inline constexpr const char* kFirmwareVersion = "firmware.version";
inline constexpr const char* kRuntimeVersion = "runtime.version";
inline constexpr const char* kPlatformGeneration = "platform.generation";
inline constexpr const char* kOs = "os.name";
inline constexpr const char* kKernelVersion = "kernel.version";
inline constexpr const char* kDeviceMode = "device.mode";
inline constexpr const char* kTopology = "topology";
inline constexpr const char* kPeerHardware = "peer.hardware";
inline constexpr const char* kPartitionState = "partition.state";
inline constexpr const char* kPrivilege = "privilege";
inline constexpr const char* kDeviceGeneration = "device.generation";
inline constexpr const char* kPlatformSettingPrefix = "platform.setting.";
inline constexpr const char* kFeatureGatePrefix = "feature.gate.";
}  // namespace env_keys

/// Immutable predicate node. Conditions are stored as a tree so that
/// conditional capability is never flattened into unconditional support.
struct ConditionNode {
  ConditionKind kind = ConditionKind::Always;
  std::string key;    ///< OS name, mode, topology, setting name, feature gate, ...
  std::string value;  ///< Required value or minimum version.
  std::vector<std::shared_ptr<const ConditionNode>> children;

  ConditionNode() = default;
  ConditionNode(ConditionKind k, std::string key_text, std::string value_text)
      : kind(k), key(std::move(key_text)), value(std::move(value_text)) {}
};

using ConditionPtr = std::shared_ptr<const ConditionNode>;

/// Structural validation: depth, node counts, child counts, leaf completeness.
Status validate_condition(const ConditionNode& node, const RegistryLimits& limits);

/// Deterministic rendering, e.g. "all_of(driver.version>=580.0, os.name==linux)".
std::string describe_condition(const ConditionNode* node);

/// Caller-supplied environment used to evaluate conditions. Absent facts
/// evaluate to Unknown, never to Satisfied and never to Unsatisfied.
class EnvironmentContext {
 public:
  void set(std::string key, std::string value);
  void set_driver_version(std::string_view v) { set(env_keys::kDriverVersion, std::string(v)); }
  void set_firmware_version(std::string_view v) { set(env_keys::kFirmwareVersion, std::string(v)); }
  void set_runtime_version(std::string_view v) { set(env_keys::kRuntimeVersion, std::string(v)); }
  void set_os(std::string_view v) { set(env_keys::kOs, std::string(v)); }
  void set_kernel_version(std::string_view v) { set(env_keys::kKernelVersion, std::string(v)); }
  void set_device_mode(std::string_view v) { set(env_keys::kDeviceMode, std::string(v)); }
  void set_topology(std::string_view v) { set(env_keys::kTopology, std::string(v)); }
  void set_peer_hardware(std::string_view v) { set(env_keys::kPeerHardware, std::string(v)); }
  void set_partition_state(std::string_view v) { set(env_keys::kPartitionState, std::string(v)); }
  void set_privilege(std::string_view v) { set(env_keys::kPrivilege, std::string(v)); }
  void set_platform_setting(std::string name, std::string value) {
    set(std::string(env_keys::kPlatformSettingPrefix) + name, std::move(value));
  }
  void add_feature_gate(std::string gate) {
    set(std::string(env_keys::kFeatureGatePrefix) + gate, std::string("present"));
  }

  [[nodiscard]] bool has(std::string_view key) const;
  [[nodiscard]] const std::string* find(std::string_view key) const;
  [[nodiscard]] const std::map<std::string, std::string, std::less<>>& facts() const noexcept {
    return facts_;
  }
  [[nodiscard]] bool empty() const noexcept { return facts_.empty(); }

 private:
  // std::less<> gives byte-wise ordering independent of locale.
  std::map<std::string, std::string, std::less<>> facts_;
};

enum class ConditionOutcome { Satisfied, Unsatisfied, Unknown, NotApplicable };

const char* to_string(ConditionOutcome outcome) noexcept;

/// One evaluated leaf, retained for deterministic explanation.
struct ConditionEvidence {
  std::string expression;  ///< canonical leaf rendering
  ConditionOutcome outcome = ConditionOutcome::NotApplicable;
  std::string detail;  ///< why, including observed fact where available
};

struct ConditionEvaluation {
  ConditionOutcome outcome = ConditionOutcome::NotApplicable;
  /// Leaf evaluations in deterministic tree order; only leaves that influenced
  /// the result are recorded for AllOf/AnyOf short-circuit determinism. Not is
  /// recorded with its single child.
  std::vector<ConditionEvidence> evidence;

  [[nodiscard]] bool decisive() const noexcept {
    return outcome == ConditionOutcome::Satisfied || outcome == ConditionOutcome::Unsatisfied;
  }
};

/// Evaluates a condition tree against an environment. A null condition is
/// unconditional and yields NotApplicable.
ConditionEvaluation evaluate_condition(const ConditionNode* node, const EnvironmentContext& env);

}  // namespace hcr
