// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// hcr - read-only inspection tool and service host.
//
// Read commands answer from either a live coordinator (--port) or a persisted
// state file (--state), using the same public API a downstream consumer uses.
// Administrative mutation is separated into explicit subcommands (fence, stop)
// and is never reachable from a read command.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "hcr/adapters.hpp"
#include "hcr/catalog.hpp"
#include "hcr/client.hpp"
#include "hcr/coordinator.hpp"
#include "hcr/discovery.hpp"
#include "hcr/persistence.hpp"
#include "hcr/publisher.hpp"
#include "hcr/registry.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"
#include "hcr/version.hpp"

namespace {

using namespace hcr;
using namespace hcr::adapters;

struct Options {
  std::map<std::string, std::string> values;
  std::vector<std::string> flags;
  std::vector<std::string> positional;

  [[nodiscard]] bool has(const std::string& name) const {
    if (values.find(name) != values.end()) return true;
    for (const std::string& flag : flags) {
      if (flag == name) return true;
    }
    return false;
  }

  [[nodiscard]] std::string get(const std::string& name, const std::string& fallback = {}) const {
    const auto it = values.find(name);
    return it == values.end() ? fallback : it->second;
  }

  [[nodiscard]] std::size_t get_size(const std::string& name, std::size_t fallback) const {
    const auto it = values.find(name);
    if (it == values.end()) return fallback;
    try {
      return static_cast<std::size_t>(std::stoull(it->second));
    } catch (...) {
      return fallback;
    }
  }
};

Options parse(int argc, char** argv, int start) {
  Options options;
  for (int i = start; i < argc; ++i) {
    const std::string token = argv[i];
    if (token.size() > 2 && token[0] == '-' && token[1] == '-') {
      const std::string name = token.substr(2);
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        options.values[name] = argv[++i];
      } else {
        options.flags.push_back(name);
      }
    } else if (!token.empty() && token[0] == '-') {
      options.flags.push_back(token.substr(1));
    } else {
      options.positional.push_back(token);
    }
  }
  return options;
}

void print_usage() {
  std::cout << "Hardware Capability Registry " << version_string() << "\n"
            << "usage: hcr <command> [options]\n\n"
            << "read commands (answer from --port <n> or --state <file>)\n"
            << "  devices                     list hardware subjects\n"
            << "  schema                      list typed capability schemas\n"
            << "  caps --device <id>          list capabilities of a device\n"
            << "  capability --device <id> --capability <id> [--explain]\n"
            << "  unknown --device <id>       capabilities with unknown state\n"
            << "  unsupported --device <id>   capabilities positively unsupported\n"
            << "  conditional --device <id>   capabilities that are conditional\n"
            << "  stale --device <id>         capabilities whose evidence is stale\n"
            << "  contradictions --device <id>\n"
            << "  quirks [--device <id>] [--applicable]\n"
            << "  compat [--kind <k>] [--reference <kind:token>]\n"
            << "  compare --left <id> --right <id>\n"
            << "  publishers                  publisher authority and boot identities\n"
            << "  epoch                       coordinator epoch\n"
            << "  classify --device <id>      REAL / SYNTHETIC / UNSUPPORTED counting\n"
            << "  snapshot                    immutable registry snapshot summary\n"
            << "  fleet --capability <id>     which devices support a capability\n\n"
            << "administrative commands (explicitly mutating)\n"
            << "  discover [--synthetic <profile>] [--hardware] [--state <file>] [--save <file>]\n"
            << "  coordinator --port <n> [--state <file>] [--persist]\n"
            << "  publisher --port <n> --publisher <id> [--synthetic <profile>] [--hardware]\n"
            << "  fence --port <n> --publisher <id> [--reason <text>]\n"
            << "  stop --port <n>\n"
            << "  selftest\n\n"
            << "environment options for conditional evaluation\n"
            << "  --driver-version <v> --firmware-version <v> --runtime-version <v>\n"
            << "  --os <name> --kernel <v> --device-mode <m> --topology <t> --privilege <p>\n"
            << "  --setting <name=value> --gate <id> --probe-env\n";
}

EnvironmentContext build_environment(const Options& options) {
  EnvironmentContext environment;
  if (!options.get("driver-version").empty()) environment.set_driver_version(options.get("driver-version"));
  if (!options.get("firmware-version").empty()) {
    environment.set_firmware_version(options.get("firmware-version"));
  }
  if (!options.get("runtime-version").empty()) environment.set_runtime_version(options.get("runtime-version"));
  if (!options.get("os").empty()) environment.set_os(options.get("os"));
  if (!options.get("kernel").empty()) environment.set_kernel_version(options.get("kernel"));
  if (!options.get("device-mode").empty()) environment.set_device_mode(options.get("device-mode"));
  if (!options.get("topology").empty()) environment.set_topology(options.get("topology"));
  if (!options.get("peer").empty()) environment.set_peer_hardware(options.get("peer"));
  if (!options.get("partition-state").empty()) {
    environment.set_partition_state(options.get("partition-state"));
  }
  if (!options.get("privilege").empty()) environment.set_privilege(options.get("privilege"));
  for (const std::string& flag : options.flags) {
    if (flag.rfind("gate=", 0) == 0) environment.add_feature_gate(flag.substr(5));
  }
  if (options.has("probe-env")) {
    PlatformIdentity platform;
    DeviceSoftwareState software;
    std::string detail;
    if (probe_host_platform(platform, environment, software, detail).ok()) {
      if (!software.driver.version.empty()) environment.set_driver_version(software.driver.version);
      if (!software.firmware.version.empty()) environment.set_firmware_version(software.firmware.version);
      if (!software.runtime.version.empty()) environment.set_runtime_version(software.runtime.version);
    }
  }
  return environment;
}

/// Uniform query surface over a live coordinator or a persisted state file, so
/// every read command works identically against both.
class Source {
 public:
  virtual ~Source() = default;
  virtual Status query_capability(const CapabilityQuery&, ResolvedCapability&) = 0;
  virtual Status query_capabilities(const DeviceCapabilityQuery&, std::vector<ResolvedCapability>&) = 0;
  virtual Status query_fleet(const FleetCapabilityQuery&, std::vector<ResolvedCapability>&) = 0;
  virtual Status query_stale(const StaleCapabilityQuery&, std::vector<ResolvedCapability>&) = 0;
  virtual Status query_quirks(const QuirkQuery&, std::vector<QuirkApplication>&) = 0;
  virtual Status list_quirks(std::vector<QuirkRecord>&) = 0;
  virtual Status query_compatibility(const CompatibilityQuery&, std::vector<CompatibilityFact>&) = 0;
  virtual Status compare(const ComparisonRequest&, DeviceComparison&) = 0;
  virtual Status list_devices(std::vector<SubjectSnapshot>&) = 0;
  virtual Status list_publishers(std::vector<PublisherRecord>&) = 0;
  virtual Status snapshot(RegistrySnapshot&) = 0;
  virtual Status list_schemas(std::vector<CapabilitySchema>&) = 0;
  virtual std::string describe() const = 0;
};

class LocalSource final : public Source {
 public:
  LocalSource(CapabilityRegistry& registry, std::vector<CapabilitySchema> schemas)
      : registry_(registry), schemas_(std::move(schemas)) {}

  Status query_capability(const CapabilityQuery& query, ResolvedCapability& out) override {
    return registry_.query_capability(query, out);
  }
  Status query_capabilities(const DeviceCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return registry_.query_capabilities(query, out);
  }
  Status query_fleet(const FleetCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return registry_.query_fleet(query, out);
  }
  Status query_stale(const StaleCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return registry_.query_stale(query, out);
  }
  Status query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out) override {
    return registry_.query_quirks(query, out);
  }
  Status list_quirks(std::vector<QuirkRecord>& out) override { return registry_.list_quirks(out); }
  Status query_compatibility(const CompatibilityQuery& query, std::vector<CompatibilityFact>& out) override {
    return registry_.query_compatibility(query, out);
  }
  Status compare(const ComparisonRequest& request, DeviceComparison& out) override {
    return registry_.compare_devices(request, out);
  }
  Status list_devices(std::vector<SubjectSnapshot>& out) override { return registry_.list_devices(out); }
  Status list_publishers(std::vector<PublisherRecord>& out) override {
    return registry_.list_publishers(PublisherQuery{}, out);
  }
  Status snapshot(RegistrySnapshot& out) override { return registry_.snapshot(out); }
  Status list_schemas(std::vector<CapabilitySchema>& out) override {
    out = schemas_;
    return Status::success();
  }
  std::string describe() const override { return "local registry"; }

 private:
  CapabilityRegistry& registry_;
  std::vector<CapabilitySchema> schemas_;
};

class RemoteSource final : public Source {
 public:
  explicit RemoteSource(std::unique_ptr<CapabilityClient> client) : client_(std::move(client)) {}

  Status query_capability(const CapabilityQuery& query, ResolvedCapability& out) override {
    return client_->query_capability(query, out);
  }
  Status query_capabilities(const DeviceCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return client_->query_capabilities(query, out);
  }
  Status query_fleet(const FleetCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return client_->query_fleet(query, out);
  }
  Status query_stale(const StaleCapabilityQuery& query, std::vector<ResolvedCapability>& out) override {
    return client_->query_stale(query, out);
  }
  Status query_quirks(const QuirkQuery& query, std::vector<QuirkApplication>& out) override {
    return client_->query_quirks(query, out);
  }
  Status list_quirks(std::vector<QuirkRecord>& out) override { return client_->list_quirks(out); }
  Status query_compatibility(const CompatibilityQuery& query, std::vector<CompatibilityFact>& out) override {
    return client_->query_compatibility(query, out);
  }
  Status compare(const ComparisonRequest& request, DeviceComparison& out) override {
    return client_->compare(request, out);
  }
  Status list_devices(std::vector<SubjectSnapshot>& out) override { return client_->list_devices(out); }
  Status list_publishers(std::vector<PublisherRecord>& out) override {
    return client_->list_publishers(out);
  }
  Status snapshot(RegistrySnapshot& out) override { return client_->snapshot(out); }
  Status list_schemas(std::vector<CapabilitySchema>& out) override {
    out = builtin_capability_schemas();
    return Status::success();
  }
  std::string describe() const override { return "coordinator"; }

 private:
  std::unique_ptr<CapabilityClient> client_;
};

struct SourceBundle {
  std::unique_ptr<CapabilityRegistry> registry;
  std::unique_ptr<Source> source;
};

Status open_source(const Options& options, SourceBundle& bundle) {
  const std::string port_text = options.get("port");
  const std::string state_path = options.get("state");
  if (!port_text.empty()) {
    ClientConfig config;
    config.port = static_cast<std::uint16_t>(std::stoul(port_text));
    auto client = std::make_unique<CapabilityClient>(config);
    const Status connected = client->connect();
    if (!connected.ok()) return connected;
    bundle.source = std::make_unique<RemoteSource>(std::move(client));
    return Status::success();
  }
  if (!state_path.empty()) {
    bundle.registry = std::make_unique<CapabilityRegistry>();
    FileStateStore store(state_path);
    DurableState state;
    const Status loaded = store.load(state);
    if (!loaded.ok()) return loaded;
    ImportReport report;
    const Status imported = bundle.registry->import_durable_state(state, report);
    if (!imported.ok()) return imported;
    bundle.source = std::make_unique<LocalSource>(*bundle.registry, bundle.registry->capability_schemas());
    return Status::success();
  }
  return Status::failure(ErrorCode::InvalidArgument, "a read command requires --port <n> or --state <file>");
}

DeviceCapabilityQuery device_query(const Options& options) {
  DeviceCapabilityQuery query;
  query.device = DeviceId::parse(options.get("device"));
  query.environment = build_environment(options);
  query.capability_prefix = options.get("prefix");
  query.only_current = !options.has("include-historical");
  if (options.has("domain")) {
    CapabilityDomain domain = CapabilityDomain::Other;
    if (capability_domain_from_string(options.get("domain"), domain)) query.domain = domain;
  }
  return query;
}

int report(const Status& status) {
  std::cerr << "error: " << status.describe() << "\n";
  return 1;
}

// -- read commands ---------------------------------------------------------

int cmd_devices(const Options&, Source& source) {
  std::vector<SubjectSnapshot> subjects;
  const Status status = source.list_devices(subjects);
  if (!status.ok()) return report(status);
  std::cout << subjects.size() << " subject(s) from " << source.describe() << "\n";
  for (const SubjectSnapshot& subject : subjects) {
    std::cout << render_subject(subject) << "\n";
  }
  return 0;
}

int cmd_schema(const Options&, Source& source) {
  std::vector<CapabilitySchema> schemas;
  const Status status = source.list_schemas(schemas);
  if (!status.ok()) return report(status);
  std::cout << schemas.size() << " capability schema(s)\n";
  for (const CapabilitySchema& schema : schemas) {
    std::cout << schema.id.str() << " | " << to_string(schema.domain) << " | " << to_string(schema.value_kind)
              << " | " << schema.capability.str() << " | " << schema.canonical_name;
    if (!schema.unit.empty()) std::cout << " | unit=" << schema.unit;
    std::cout << "\n";
  }
  return 0;
}

int cmd_caps(const Options& options, Source& source) {
  std::vector<ResolvedCapability> capabilities;
  const Status status = source.query_capabilities(device_query(options), capabilities);
  if (!status.ok()) return report(status);
  std::cout << capabilities.size() << " capability record(s)\n";
  for (const ResolvedCapability& capability : capabilities) {
    std::cout << render_capability_brief(capability) << "\n";
  }
  return 0;
}

int cmd_capability(const Options& options, Source& source) {
  CapabilityQuery query;
  query.device = DeviceId::parse(options.get("device"));
  query.capability = CapabilityId::parse(options.get("capability"));
  query.environment = build_environment(options);
  ResolvedCapability resolved;
  const Status status = source.query_capability(query, resolved);
  if (!status.ok()) return report(status);
  std::cout << (options.has("explain") ? render_capability(resolved) : render_capability_brief(resolved))
            << "\n";
  return 0;
}

int cmd_filtered(const Options& options, Source& source, const char* which) {
  DeviceCapabilityQuery query = device_query(options);
  const std::string filter = which;
  if (filter == "unknown") {
    query.only_unknown = true;
    query.only_current = false;
  } else if (filter == "unsupported") {
    query.only_unsupported = true;
    query.only_current = false;
  } else if (filter == "conditional") {
    query.only_conditional = true;
    query.only_current = false;
  }
  std::vector<ResolvedCapability> capabilities;
  const Status status = source.query_capabilities(query, capabilities);
  if (!status.ok()) return report(status);
  std::cout << capabilities.size() << " " << filter << " capability record(s)\n";
  for (const ResolvedCapability& capability : capabilities) {
    std::cout << render_capability_brief(capability) << "\n";
  }
  return 0;
}

int cmd_stale(const Options& options, Source& source) {
  StaleCapabilityQuery query;
  query.device = DeviceId::parse(options.get("device"));
  query.environment = build_environment(options);
  std::vector<ResolvedCapability> capabilities;
  const Status status = source.query_stale(query, capabilities);
  if (!status.ok()) return report(status);
  std::cout << capabilities.size() << " stale or non-current capability record(s)\n";
  for (const ResolvedCapability& capability : capabilities) {
    std::cout << render_capability_brief(capability) << "\n";
  }
  return 0;
}

int cmd_contradictions(const Options& options, Source& source) {
  DeviceCapabilityQuery query = device_query(options);
  query.only_current = false;
  std::vector<ResolvedCapability> capabilities;
  const Status status = source.query_capabilities(query, capabilities);
  if (!status.ok()) return report(status);
  std::size_t count = 0;
  for (const ResolvedCapability& capability : capabilities) {
    if (capability.contradiction == ContradictionState::Consistent) continue;
    ++count;
    std::cout << render_capability_brief(capability) << "\n" << render_capability(capability) << "\n";
  }
  std::cout << count << " contradicted capability record(s)\n";
  return 0;
}

int cmd_quirks(const Options& options, Source& source) {
  QuirkQuery query;
  query.environment = build_environment(options);
  if (!options.get("device").empty()) query.device = DeviceId::parse(options.get("device"));
  if (!options.get("capability").empty()) query.capability = CapabilityId::parse(options.get("capability"));
  query.only_applicable = options.has("applicable");
  if (options.has("all")) {
    std::vector<QuirkRecord> quirks;
    const Status status = source.list_quirks(quirks);
    if (!status.ok()) return report(status);
    std::cout << quirks.size() << " quirk record(s)\n";
    for (const QuirkRecord& quirk : quirks) std::cout << render_quirk(quirk) << "\n";
    return 0;
  }
  std::vector<QuirkApplication> applications;
  const Status status = source.query_quirks(query, applications);
  if (!status.ok()) return report(status);
  std::cout << applications.size() << " quirk evaluation(s)\n";
  for (const QuirkApplication& application : applications) {
    std::cout << render_quirk_application(application) << "\n";
  }
  return 0;
}

int cmd_compat(const Options& options, Source& source) {
  CompatibilityQuery query;
  query.include_withdrawn = options.has("include-withdrawn");
  if (options.has("kind")) {
    CompatibilitySubjectKind kind = CompatibilitySubjectKind::Other;
    if (compatibility_subject_kind_from_string(options.get("kind"), kind)) query.kind = kind;
  }
  if (options.has("reference")) {
    const std::string reference = options.get("reference");
    const std::size_t separator = reference.find(':');
    if (separator == std::string::npos) {
      return report(Status::failure(ErrorCode::InvalidArgument, "--reference expects <kind>:<token>"));
    }
    HardwareReference hardware;
    if (!hardware_reference_kind_from_string(reference.substr(0, separator), hardware.kind)) {
      return report(Status::failure(ErrorCode::InvalidArgument, "unknown reference kind"));
    }
    hardware.token = reference.substr(separator + 1);
    query.reference = hardware;
  }
  std::vector<CompatibilityFact> facts;
  const Status status = source.query_compatibility(query, facts);
  if (!status.ok()) return report(status);
  std::cout << facts.size() << " compatibility fact(s)\n";
  for (const CompatibilityFact& fact : facts) std::cout << render_compatibility(fact) << "\n";
  return 0;
}

int cmd_compare(const Options& options, Source& source) {
  ComparisonRequest request;
  request.left = DeviceId::parse(options.get("left"));
  request.right = DeviceId::parse(options.get("right"));
  request.environment = build_environment(options);
  DeviceComparison comparison;
  const Status status = source.compare(request, comparison);
  if (!status.ok()) return report(status);
  std::cout << render_comparison(comparison) << "\n";
  return 0;
}

int cmd_publishers(const Options& options, Source& source) {
  (void)options;
  std::vector<PublisherRecord> publishers;
  const Status status = source.list_publishers(publishers);
  if (!status.ok()) return report(status);
  std::cout << publishers.size() << " publisher(s)\n";
  for (const PublisherRecord& publisher : publishers) std::cout << render_publisher(publisher) << "\n";
  return 0;
}

int cmd_epoch(const Options& options, Source& source) {
  (void)options;
  RegistrySnapshot snapshot;
  const Status status = source.snapshot(snapshot);
  if (!status.ok()) return report(status);
  std::cout << "coordinator epoch " << snapshot.epoch.str() << " snapshot generation "
            << snapshot.generation.str() << " state revision " << snapshot.state_revision << "\n";
  return 0;
}

int cmd_classify(const Options& options, Source& source) {
  std::vector<ResolvedCapability> capabilities;
  DeviceCapabilityQuery query = device_query(options);
  query.only_current = false;
  const Status status = source.query_capabilities(query, capabilities);
  if (!status.ok()) return report(status);
  std::size_t real = 0;
  std::size_t synthetic = 0;
  std::size_t unsupported = 0;
  std::size_t unknown = 0;
  for (const ResolvedCapability& capability : capabilities) {
    switch (capability.truth) {
      case TruthClass::Real: ++real; break;
      case TruthClass::Synthetic: ++synthetic; break;
      case TruthClass::Unsupported: ++unsupported; break;
      case TruthClass::Unknown: ++unknown; break;
    }
  }
  std::cout << "REAL=" << real << " SYNTHETIC=" << synthetic << " UNSUPPORTED=" << unsupported
            << " UNKNOWN=" << unknown << "\n";
  return 0;
}

int cmd_snapshot(const Options& options, Source& source) {
  (void)options;
  RegistrySnapshot snapshot;
  const Status status = source.snapshot(snapshot);
  if (!status.ok()) return report(status);
  std::cout << render_snapshot_summary(snapshot) << "\n";
  return 0;
}

int cmd_fleet(const Options& options, Source& source) {
  FleetCapabilityQuery query;
  query.capability = CapabilityId::parse(options.get("capability"));
  query.environment = build_environment(options);
  std::vector<ResolvedCapability> capabilities;
  const Status status = source.query_fleet(query, capabilities);
  if (!status.ok()) return report(status);
  std::cout << capabilities.size() << " device(s)\n";
  for (const ResolvedCapability& capability : capabilities) {
    std::cout << capability.device.str() << " " << render_capability_brief(capability) << "\n";
  }
  return 0;
}

// -- administrative commands ----------------------------------------------

struct DiscoveryPlan {
  std::vector<std::unique_ptr<IDiscoveryBackend>> backends;
  std::vector<std::string> labels;
};

int run_discovery(const Options& options, CapabilityRegistry& registry, EvidenceSink& sink) {
  PlatformIdentity platform;
  EnvironmentContext environment;
  DeviceSoftwareState software;
  std::string detail;
  const Status probed = probe_host_platform(platform, environment, software, detail);
  if (probed.ok()) {
    std::cout << detail << "\n";
  } else {
    platform.id = PlatformId::parse("platform.host.unknown");
    platform.display_name = "unknown host";
    platform.vendor = "unknown";
    platform.model = "unknown";
    platform.architecture = ArchitectureId::parse("arch.unknown");
    std::cout << "host platform probe unavailable: " << probed.message() << "\n";
  }

  DiscoveryContext context;
  context.publisher.id = PublisherId::parse(options.get("publisher", "publisher.local"));
  context.platform = platform;
  context.software = software;
  context.environment = environment;
  context.observed_at_utc = utc_now_iso8601();

  std::vector<std::unique_ptr<IDiscoveryBackend>> backends;
  if (options.has("hardware") || (!options.has("synthetic") && options.positional.empty())) {
    backends.push_back(make_cpu_platform_backend());
    backends.push_back(make_pci_topology_backend());
    backends.push_back(make_nvml_backend());
    backends.push_back(make_cuda_runtime_backend());
    backends.push_back(make_network_backend());
    backends.push_back(make_cxl_backend());
  }
  if (options.has("synthetic")) {
    SyntheticProfile profile = SyntheticProfile::MixedVendorFleet;
    if (!synthetic_profile_from_string(options.get("synthetic"), profile)) {
      std::cerr << "error: unknown synthetic profile " << options.get("synthetic") << "\n";
      return 1;
    }
    backends.push_back(make_synthetic_backend(profile));
  }
  for (const std::string& positional : options.positional) {
    SyntheticProfile profile = SyntheticProfile::MixedVendorFleet;
    if (synthetic_profile_from_string(positional, profile)) {
      backends.push_back(make_synthetic_backend(profile));
    }
  }

  int failures = 0;
  for (const auto& backend : backends) {
    DiscoveryReport report;
    const Status status = run_discovery(*backend, context, sink, report);
    std::cout << (report.available ? "[ran]     " : "[skipped] ") << report.adapter << " accepted="
              << report.accepted << " idempotent=" << report.idempotent << " rejected=" << report.rejected
              << "\n";
    for (const std::string& note : report.notes) std::cout << "          note: " << note << "\n";
    if (!status.ok()) {
      std::cout << "          " << status.describe() << "\n";
      ++failures;
    }
  }
  std::cout << render_resource_usage(registry.resource_usage()) << "\n";
  return failures;
}

int cmd_discover(const Options& options) {
  CapabilityRegistry registry;
  LocalEvidenceSink sink(registry);
  const int failures = run_discovery(options, registry, sink);
  if (options.has("save")) {
    FileStateStore store(options.get("save"));
    const DurableState state = registry.export_durable_state();
    const Status saved = store.save(state);
    if (!saved.ok()) return report(saved);
    std::cout << "saved durable state to " << store.path() << "\n";
  }
  if (options.has("summary")) {
    std::vector<SubjectSnapshot> subjects;
    registry.list_devices(subjects);
    for (const SubjectSnapshot& subject : subjects) std::cout << render_subject(subject) << "\n";
  }
  return failures == 0 ? 0 : 1;
}

int cmd_coordinator(const Options& options) {
  CoordinatorConfig config;
  config.port = static_cast<std::uint16_t>(options.get_size("port", 0));
  config.state_path = options.get("state");
  config.persist_on_mutation = options.has("persist");
  config.node_name = options.get("name", "hcr-coordinator");
  CapabilityCoordinator coordinator(config);
  const Status started = coordinator.start();
  if (!started.ok()) return report(started);
  const std::uint16_t port = coordinator.port();
  std::cout << "coordinator listening on 127.0.0.1:" << port << " epoch " << coordinator.epoch().str() << "\n";
  std::cout.flush();
  const Status served = coordinator.run(nullptr);
  if (!served.ok()) return report(served);
  std::cout << "coordinator stopped; sessions fenced" << "\n";
  return 0;
}

int cmd_publisher(const Options& options) {
  PublisherConfig config;
  config.port = static_cast<std::uint16_t>(options.get_size("port", 0));
  config.publisher = PublisherId::parse(options.get("publisher"));
  config.adapter = options.get("adapter", "cli.publisher");
  CapabilityPublisher publisher(config);
  const Status registered = publisher.connect_and_register();
  if (!registered.ok()) return report(registered);
  std::cout << "publisher " << publisher.identity().id.str() << " boot " << publisher.identity().boot.str()
            << " epoch " << publisher.identity().epoch.str() << "\n";
  std::cout.flush();

  PlatformIdentity platform;
  EnvironmentContext environment;
  DeviceSoftwareState software;
  std::string detail;
  if (!probe_host_platform(platform, environment, software, detail).ok()) {
    platform.id = PlatformId::parse("platform.host.unknown");
    platform.display_name = "unknown host";
  }
  DiscoveryContext context;
  context.publisher = publisher.identity();
  context.platform = platform;
  context.software = software;
  context.environment = environment;
  context.observed_at_utc = utc_now_iso8601();

  std::vector<std::unique_ptr<IDiscoveryBackend>> backends;
  if (options.has("synthetic")) {
    SyntheticProfile profile = SyntheticProfile::MixedVendorFleet;
    if (!synthetic_profile_from_string(options.get("synthetic"), profile)) {
      std::cerr << "error: unknown synthetic profile\n";
      return 1;
    }
    backends.push_back(make_synthetic_backend(profile));
  }
  if (options.has("hardware") || backends.empty()) {
    backends.push_back(make_cpu_platform_backend());
    backends.push_back(make_pci_topology_backend());
    backends.push_back(make_nvml_backend());
    backends.push_back(make_cuda_runtime_backend());
    backends.push_back(make_network_backend());
    backends.push_back(make_cxl_backend());
  }
  int failures = 0;
  for (const auto& backend : backends) {
    DiscoveryReport report;
    const Status status = run_discovery(*backend, context, publisher, report);
    std::cout << (report.available ? "[ran]     " : "[skipped] ") << report.adapter << " accepted="
              << report.accepted << " rejected=" << report.rejected << "\n";
    for (const std::string& note : report.notes) std::cout << "          note: " << note << "\n";
    if (!status.ok()) {
      std::cout << "          " << status.describe() << "\n";
      ++failures;
    }
  }
  std::cout.flush();
  if (options.has("linger")) {
    // Keeps the publisher process alive so that process-death scenarios have a
    // real, killable publisher instead of a synthetic stand-in. The wait is a
    // blocking exchange with the coordinator, not a timeout: the loop ends when
    // the coordinator closes the session, so a dead coordinator terminates its
    // publishers and a live one keeps them authoritative.
    std::cout << "lingering\n";
    std::cout.flush();
    while (publisher.connected()) {
      const ApplyResult heartbeat = publisher.heartbeat();
      if (!heartbeat.accepted() && heartbeat.code != ApplyCode::AcceptedIdempotent) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "coordinator session ended\n";
    std::cout.flush();
  }
  publisher.disconnect();
  return failures == 0 ? 0 : 1;
}

int cmd_fence(const Options& options) {
  ClientConfig config;
  config.port = static_cast<std::uint16_t>(options.get_size("port", 0));
  CapabilityClient client(config);
  const Status connected = client.connect();
  if (!connected.ok()) return report(connected);
  const Status status = client.fence_publisher(PublisherId::parse(options.get("publisher")),
                                               options.get("reason", "fenced by operator"));
  if (!status.ok()) return report(status);
  std::cout << "publisher fenced\n";
  return 0;
}

int cmd_stop(const Options& options) {
  ClientConfig config;
  config.port = static_cast<std::uint16_t>(options.get_size("port", 0));
  CapabilityClient client(config);
  const Status connected = client.connect();
  if (!connected.ok()) return report(connected);
  const Status status = client.shutdown(options.get("reason", "operator request"));
  if (!status.ok()) return report(status);
  std::cout << "shutdown accepted\n";
  return 0;
}

int cmd_selftest() {
  CapabilityRegistry registry;
  LocalEvidenceSink sink(registry);
  Options options;
  options.values["synthetic"] = "mixed-vendor-fleet";
  const int failures = run_discovery(options, registry, sink);
  std::vector<SubjectSnapshot> subjects;
  registry.list_devices(subjects);
  std::cout << "selftest subjects=" << subjects.size() << "\n";
  RegistrySnapshot snapshot;
  const Status status = registry.snapshot(snapshot);
  if (!status.ok()) return report(status);
  std::cout << render_snapshot_summary(snapshot) << "\n";
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }
  const std::string command = argv[1];
  const Options options = parse(argc, argv, 2);

  if (command == "version" || command == "--version") {
    std::cout << "Hardware Capability Registry " << version_string() << " protocol "
              << protocol_version() << " state-format " << state_format_version() << "\n";
    return 0;
  }
  if (command == "help" || command == "--help" || command == "-h") {
    print_usage();
    return 0;
  }
  if (command == "discover") return cmd_discover(options);
  if (command == "coordinator") return cmd_coordinator(options);
  if (command == "publisher") return cmd_publisher(options);
  if (command == "fence") return cmd_fence(options);
  if (command == "stop") return cmd_stop(options);
  if (command == "selftest") return cmd_selftest();

  SourceBundle bundle;
  const Status opened = open_source(options, bundle);
  if (!opened.ok()) return report(opened);
  Source& source = *bundle.source;

  if (command == "devices") return cmd_devices(options, source);
  if (command == "schema") return cmd_schema(options, source);
  if (command == "caps") return cmd_caps(options, source);
  if (command == "capability") return cmd_capability(options, source);
  if (command == "unknown") return cmd_filtered(options, source, "unknown");
  if (command == "unsupported") return cmd_filtered(options, source, "unsupported");
  if (command == "conditional") return cmd_filtered(options, source, "conditional");
  if (command == "stale") return cmd_stale(options, source);
  if (command == "contradictions") return cmd_contradictions(options, source);
  if (command == "quirks") return cmd_quirks(options, source);
  if (command == "compat") return cmd_compat(options, source);
  if (command == "compare") return cmd_compare(options, source);
  if (command == "publishers") return cmd_publishers(options, source);
  if (command == "epoch") return cmd_epoch(options, source);
  if (command == "classify") return cmd_classify(options, source);
  if (command == "snapshot") return cmd_snapshot(options, source);
  if (command == "fleet") return cmd_fleet(options, source);

  std::cerr << "error: unknown command '" << command << "'\n";
  print_usage();
  return 2;
}
