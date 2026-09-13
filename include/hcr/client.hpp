// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hcr/query.hpp"
#include "hcr/snapshot.hpp"
#include "hcr/status.hpp"

namespace hcr {

struct ClientConfig {
  std::uint16_t port = 0;
  std::string agent = "hcr-client";
  RegistryLimits limits;
};

/// Read-only capability client. Administrative mutation (fencing) is exposed
/// separately and explicitly; queries never mutate registry state.
class CapabilityClient {
 public:
  explicit CapabilityClient(ClientConfig config);
  ~CapabilityClient();

  CapabilityClient(const CapabilityClient&) = delete;
  CapabilityClient& operator=(const CapabilityClient&) = delete;

  Status connect();
  Status disconnect();
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] CoordinatorEpoch epoch() const noexcept;

  Status query_capability(const CapabilityQuery& query, ResolvedCapability& out);
  Status query_capabilities(const DeviceCapabilityQuery& query, std::vector<ResolvedCapability>& out);
  Status query_fleet(const FleetCapabilityQuery& query, std::vector<ResolvedCapability>& out);
  Status query_stale(const StaleCapabilityQuery& query, std::vector<ResolvedCapability>& out);
  Status query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out);
  Status list_quirks(std::vector<QuirkRecord>& out);
  Status query_compatibility(const CompatibilityQuery& query, std::vector<CompatibilityFact>& out);
  Status compare(const ComparisonRequest& request, DeviceComparison& out);
  Status list_devices(std::vector<SubjectSnapshot>& out);
  Status list_publishers(std::vector<PublisherRecord>& out);
  Status snapshot(RegistrySnapshot& out);
  Status explain(const CapabilityQuery& query, std::string& out);

  /// Administrative: permanently removes authority from a publisher.
  Status fence_publisher(const PublisherId& publisher, std::string reason);
  /// Administrative: requests coordinator shutdown after committed state is
  /// persisted.
  Status shutdown(std::string reason);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hcr
