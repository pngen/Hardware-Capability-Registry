// Hardware Capability Registry - distributed authority and recovery tests.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// These tests spawn real OS processes through the hcr command line tool. No
// test applies a timeout: readiness is observed by blocking on the child's
// output pipe, and death is observed by waiting on the process handle. A hang
// is a defect, not something the harness hides.
#include <cstdio>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "harness.hpp"
#include "hcr/client.hpp"
#include "hcr/protocol.hpp"
#include "hcr/transport.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

using namespace hcr;
using namespace hcrtest;

namespace {

#if defined(_WIN32)

/// A real child process with piped standard input and output.
class ChildProcess {
 public:
  ChildProcess() = default;
  ~ChildProcess() {
    close_handles();
    if (running) terminate();
  }

  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;

  bool spawn(const std::string& executable, const std::string& arguments) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write_local = nullptr;
    HANDLE stdout_read_local = nullptr;
    HANDLE stdout_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write_local, &attributes, 0)) return false;
    if (!CreatePipe(&stdout_read_local, &stdout_write, &attributes, 0)) return false;
    SetHandleInformation(stdin_write_local, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read_local, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stdout_write;

    std::string command_line = "\"" + executable + "\" " + arguments;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                        nullptr, &startup, &process);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (!created) {
      CloseHandle(stdin_write_local);
      CloseHandle(stdout_read_local);
      return false;
    }
    process_ = process.hProcess;
    thread_ = process.hThread;
    stdin_write_ = stdin_write_local;
    stdout_read_ = stdout_read_local;
    running = true;
    return true;
  }

  /// Blocks until one complete line arrives. Returns false at end of output,
  /// which is the only condition that stops the wait: no timeout is applied.
  bool read_line(std::string& line, std::string& collected) {
    line.clear();
    char byte = 0;
    DWORD read = 0;
    while (true) {
      if (!ReadFile(stdout_read_, &byte, 1, &read, nullptr) || read == 0) return false;
      if (byte == '\n') {
        collected += line;
        collected += "\n";
        return true;
      }
      if (byte != '\r') line.push_back(byte);
    }
  }

  /// Blocks until a line contains the marker, collecting everything seen.
  std::string read_until(const std::string& marker, std::string& collected) {
    std::string line;
    while (read_line(line, collected)) {
      if (line.find(marker) != std::string::npos) return line;
    }
    return {};
  }

  bool terminate() {
    if (!running || process_ == nullptr) return false;
    const BOOL killed = TerminateProcess(process_, 7);
    DWORD code = 0;
    WaitForSingleObject(process_, INFINITE);
    GetExitCodeProcess(process_, &code);
    running = false;
    return killed != FALSE;
  }

  bool wait_for_exit(unsigned long& exit_code) {
    if (process_ == nullptr) return false;
    WaitForSingleObject(process_, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    exit_code = code;
    running = false;
    return true;
  }

  void close_stdin() {
    if (stdin_write_ != nullptr) {
      CloseHandle(stdin_write_);
      stdin_write_ = nullptr;
    }
  }

  [[nodiscard]] bool alive() const { return running; }

 private:
  void close_handles() {
    if (stdin_write_ != nullptr) CloseHandle(stdin_write_);
    if (stdout_read_ != nullptr) CloseHandle(stdout_read_);
    if (thread_ != nullptr) CloseHandle(thread_);
    if (process_ != nullptr) CloseHandle(process_);
    stdin_write_ = nullptr;
    stdout_read_ = nullptr;
    thread_ = nullptr;
    process_ = nullptr;
  }

  HANDLE process_ = nullptr;
  HANDLE thread_ = nullptr;
  HANDLE stdin_write_ = nullptr;
  HANDLE stdout_read_ = nullptr;
  bool running = false;
};

std::string cli_path() {
#ifdef HCR_CLI_PATH
  return HCR_CLI_PATH;
#else
  return {};
#endif
}

std::uint16_t free_loopback_port() {
  TcpListener listener;
  if (!listener.open_loopback(0, 4).ok()) return 0;
  std::uint16_t port = 0;
  listener.local_port(port);
  listener.close();
  return port;
}

std::string temporary_state_path(const char* name) {
  return std::string("hcr-distributed-") + name + ".state";
}

void remove_file(const std::string& path) { std::remove(path.c_str()); }

/// Starts a coordinator and blocks until it reports that it is listening.
bool start_coordinator(ChildProcess& process, std::uint16_t port, const std::string& state_path,
                       std::string& output, std::uint64_t& epoch) {
  std::string arguments = "coordinator --port " + std::to_string(port);
  if (!state_path.empty()) arguments += " --state \"" + state_path + "\"";
  if (!process.spawn(cli_path(), arguments)) return false;
  const std::string line = process.read_until("listening", output);
  if (line.empty()) return false;
  const std::size_t marker = line.find("epoch ");
  if (marker != std::string::npos) {
    try {
      epoch = std::stoull(line.substr(marker + 6));
    } catch (...) {
      epoch = 0;
    }
  }
  return true;
}

bool start_publisher(ChildProcess& process, std::uint16_t port, const char* publisher_id,
                     const char* profile, std::string& output) {
  const std::string arguments = std::string("publisher --port ") + std::to_string(port) +
                                " --publisher " + publisher_id + " --synthetic " + profile + " --linger";
  if (!process.spawn(cli_path(), arguments)) return false;
  const std::string line = process.read_until("lingering", output);
  return !line.empty();
}

std::unique_ptr<CapabilityClient> connect_client(std::uint16_t port) {
  ClientConfig config;
  config.port = port;
  auto client = std::make_unique<CapabilityClient>(config);
  const Status status = client->connect();
  if (!status.ok()) return nullptr;
  return client;
}

ResolvedCapability query_or_fail(CapabilityClient& client, const char* device_token,
                                 const char* capability) {
  CapabilityQuery query;
  query.device = canonical_device_id("dev.synthetic", device_token);
  query.capability = CapabilityId::parse(capability);
  ResolvedCapability resolved;
  const Status status = client.query_capability(query, resolved);
  if (!status.ok()) throw hcrtest::Failure(status.describe());
  return resolved;
}

/// Sends a single raw frame from a socket that is not a registered publisher.
struct RawConnection {
  TcpConnection connection;
  FrameDecoder decoder{RegistryLimits{}};

  bool open(std::uint16_t port) { return connect_loopback(port, connection).ok(); }

  Status handshake() {
    HelloMessage hello;
    hello.protocol_version = protocol_version();
    hello.agent = "raw-test";
    hello.role = "publisher";
    std::vector<std::uint8_t> payload;
    Status status = encode_hello(hello, RegistryLimits{}, payload);
    if (!status.ok()) return status;
    Frame frame;
    frame.type = MessageType::Hello;
    frame.correlation = 1;
    frame.payload = std::move(payload);
    status = send_frame(connection, frame, RegistryLimits{});
    if (!status.ok()) return status;
    Frame response;
    status = receive_frame(connection, decoder, response);
    if (!status.ok()) return status;
    if (response.type != MessageType::Welcome) {
      return Status::failure(ErrorCode::ProtocolError, "raw handshake did not receive WELCOME");
    }
    return Status::success();
  }

  Status send_publication(const Publication& publication, ApplyCode& code, std::string& detail) {
    std::vector<std::uint8_t> payload;
    Status status = encode_publication(publication, RegistryLimits{}, payload);
    if (!status.ok()) return status;
    Frame frame;
    frame.type = MessageType::Publication;
    frame.correlation = 2;
    frame.payload = std::move(payload);
    status = send_frame(connection, frame, RegistryLimits{});
    if (!status.ok()) return status;
    Frame response;
    status = receive_frame(connection, decoder, response);
    if (!status.ok()) return status;
    if (response.type == MessageType::ErrorResponse) {
      ErrorMessage error;
      status = decode_error_message(response.payload.data(), response.payload.size(), RegistryLimits{}, error);
      if (!status.ok()) return status;
      code = ApplyCode::RejectedValidation;
      detail = error.message;
      return Status::success();
    }
    PublicationAcceptedMessage accepted;
    status = decode_publication_accepted(response.payload.data(), response.payload.size(), RegistryLimits{},
                                         accepted);
    if (!status.ok()) return status;
    code = accepted.code;
    detail = accepted.detail;
    return Status::success();
  }
};

#endif  // _WIN32

}  // namespace

HCR_TEST(distributed, publisher_death_reincarnation_and_replay_proof) {
#if !defined(_WIN32)
  HCR_SKIP("distributed proofs require the Windows process and socket implementation");
#else
  if (cli_path().empty()) HCR_SKIP("the hcr command line tool was not built");
  const std::uint16_t port = free_loopback_port();
  HCR_REQUIRE(port != 0);

  ChildProcess coordinator;
  std::string coordinator_output;
  std::uint64_t epoch = 0;
  HCR_REQUIRE_MSG(start_coordinator(coordinator, port, "", coordinator_output, epoch),
                  "coordinator did not start: " + coordinator_output);

  ChildProcess publisher_b;
  std::string publisher_b_output;
  HCR_REQUIRE_MSG(start_publisher(publisher_b, port, "publisher.b", "cxl-fabric", publisher_b_output),
                  "publisher b did not start: " + publisher_b_output);

  ChildProcess publisher_a;
  std::string publisher_a_output;
  HCR_REQUIRE(start_publisher(publisher_a, port, "publisher.a", "unsupported-scenario",
                              publisher_a_output));

  auto client = connect_client(port);
  HCR_REQUIRE(client != nullptr);

  const ResolvedCapability before = query_or_fail(*client, "rtx4090", "cap.partition.mechanism");
  HCR_CHECK(before.effective_support == EffectiveSupport::Unsupported);
  HCR_CHECK(before.truth == TruthClass::Unsupported);
  HCR_CHECK(before.current);

  std::vector<PublisherRecord> publishers;
  HCR_REQUIRE(client->list_publishers(publishers).ok());
  PublisherBootId publisher_a_boot;
  bool found_a = false;
  for (const PublisherRecord& record : publishers) {
    if (record.identity.id == PublisherId::parse("publisher.a")) {
      publisher_a_boot = record.identity.boot;
      found_a = true;
      HCR_CHECK(record.state == PublisherState::Registered);
    }
  }
  HCR_CHECK(found_a);

  // Real process death.
  HCR_CHECK(publisher_a.terminate());

  const ResolvedCapability after_death = query_or_fail(*client, "rtx4090", "cap.partition.mechanism");
  HCR_CHECK(after_death.effective_support == EffectiveSupport::RevalidationRequired);
  HCR_CHECK(!after_death.current);
  HCR_CHECK(is_stale(after_death.staleness));

  HCR_REQUIRE(client->list_publishers(publishers).ok());
  for (const PublisherRecord& record : publishers) {
    if (record.identity.id == PublisherId::parse("publisher.a")) {
      HCR_CHECK(record.state != PublisherState::Registered);
    }
  }

  // Publisher B is unaffected and its evidence is still current.
  const ResolvedCapability b_device = query_or_fail(*client, "cxl-1", "cap.interconnect.cxl");
  HCR_CHECK(b_device.current);
  HCR_CHECK(b_device.effective_support == EffectiveSupport::Supported);

  // Replaying the dead boot identity is refused.
  {
    RawConnection raw;
    HCR_REQUIRE(raw.open(port));
    HCR_REQUIRE(raw.handshake().ok());
    Publication replay;
    replay.kind = PublicationKind::PublishCapability;
    replay.publisher.id = PublisherId::parse("publisher.a");
    replay.publisher.boot = publisher_a_boot;
    replay.publisher.epoch = CoordinatorEpoch{epoch};
    replay.sequence = 100000;
    replay.evidence_generation = EvidenceGeneration{999};
    replay.capability.device = canonical_device_id("dev.synthetic", "rtx4090");
    replay.capability.device_generation = DeviceGeneration::first();
    replay.capability.device_boot = DeviceBootId::first();
    replay.capability.capability = CapabilityId::parse("cap.partition.mechanism");
    replay.capability.schema = CapabilitySchemaId::parse("cap.partition.mechanism.v1");
    replay.capability.support = SupportState::SupportedNative;
    replay.capability.value = CapabilityValue(FeatureSetValue{{"mig"}});
    replay.capability.source.source_class = SourceClass::SyntheticBackend;
    replay.capability.source.provenance = ProvenanceClass::Synthetic;
    replay.capability.source.adapter = "replay";
    ApplyCode code = ApplyCode::Accepted;
    std::string detail;
    HCR_REQUIRE(raw.send_publication(replay, code, detail).ok());
    HCR_CHECK_MSG(code == ApplyCode::RejectedFencedPublisher || code == ApplyCode::RejectedStaleBoot ||
                      code == ApplyCode::RejectedGenerationRegression,
                  detail);
  }

  // A replacement publisher receives a fresh boot identity and republishing
  // makes the capability current again.
  ChildProcess publisher_a_prime;
  std::string publisher_a_prime_output;
  HCR_REQUIRE(start_publisher(publisher_a_prime, port, "publisher.a", "unsupported-scenario",
                              publisher_a_prime_output));
  HCR_REQUIRE(client->list_publishers(publishers).ok());
  PublisherBootId replacement_boot;
  for (const PublisherRecord& record : publishers) {
    if (record.identity.id == PublisherId::parse("publisher.a")) {
      replacement_boot = record.identity.boot;
      HCR_CHECK(record.state == PublisherState::Registered);
    }
  }
  HCR_CHECK(replacement_boot > publisher_a_boot);

  const ResolvedCapability after_replacement =
      query_or_fail(*client, "rtx4090", "cap.partition.mechanism");
  HCR_CHECK(after_replacement.current);
  HCR_CHECK(after_replacement.effective_support == EffectiveSupport::Unsupported);

  // Clean shutdown and no orphan processes.
  HCR_CHECK(client->shutdown("test complete").ok());
  unsigned long coordinator_exit = 0;
  HCR_REQUIRE(coordinator.wait_for_exit(coordinator_exit));
  HCR_CHECK_EQ(coordinator_exit, 0ul);

  publisher_b.close_stdin();
  publisher_a_prime.close_stdin();
  unsigned long exit_code = 0;
  publisher_b.wait_for_exit(exit_code);
  publisher_a_prime.wait_for_exit(exit_code);
#endif
}

HCR_TEST(distributed, coordinator_restart_advances_epoch_and_revalidates_live_truth) {
#if !defined(_WIN32)
  HCR_SKIP("distributed proofs require the Windows process and socket implementation");
#else
  if (cli_path().empty()) HCR_SKIP("the hcr command line tool was not built");
  const std::string state_path = temporary_state_path("restart");
  remove_file(state_path);

  std::uint16_t port = free_loopback_port();
  HCR_REQUIRE(port != 0);
  ChildProcess coordinator;
  std::string coordinator_output;
  std::uint64_t first_epoch = 0;
  HCR_REQUIRE_MSG(start_coordinator(coordinator, port, state_path, coordinator_output, first_epoch),
                  "coordinator did not start: " + coordinator_output);

  ChildProcess publisher;
  std::string publisher_output;
  HCR_REQUIRE(start_publisher(publisher, port, "publisher.contradiction", "contradiction-scenario",
                              publisher_output));

  auto client = connect_client(port);
  HCR_REQUIRE(client != nullptr);
  const ResolvedCapability before = query_or_fail(*client, "a100-conflict", "cap.compute.fp8");
  HCR_CHECK(before.contradiction == ContradictionState::PreferredLiveEvidence);
  HCR_CHECK(before.declared_support == SupportState::Unsupported);
  HCR_CHECK_EQ(before.evidence.size(), std::size_t{2});

  HCR_CHECK(client->shutdown("restart proof").ok());
  client->disconnect();
  unsigned long exit_code = 0;
  HCR_REQUIRE(coordinator.wait_for_exit(exit_code));
  HCR_CHECK_EQ(exit_code, 0ul);
  publisher.terminate();

  // Restart on a fresh port with the same durable state.
  port = free_loopback_port();
  HCR_REQUIRE(port != 0);
  ChildProcess restarted;
  std::string restarted_output;
  std::uint64_t second_epoch = 0;
  HCR_REQUIRE_MSG(start_coordinator(restarted, port, state_path, restarted_output, second_epoch),
                  "restarted coordinator did not start: " + restarted_output);
  HCR_CHECK(second_epoch > first_epoch);

  auto client_two = connect_client(port);
  HCR_REQUIRE(client_two != nullptr);

  const ResolvedCapability after = query_or_fail(*client_two, "a100-conflict", "cap.compute.fp8");
  // Curated knowledge is preserved and becomes the winning evidence; the live
  // probe is retained but is no longer current.
  HCR_CHECK(after.declared_support == SupportState::SupportedNative);
  HCR_CHECK(after.precision == PrecisionClass::Curated);
  HCR_CHECK_EQ(after.evidence.size(), std::size_t{2});
  bool saw_stale_live = false;
  for (const EvidenceView& view : after.evidence) {
    if (view.source.provenance == ProvenanceClass::RealRuntimeProbe) {
      saw_stale_live = true;
      HCR_CHECK(is_stale(view.staleness));
      HCR_CHECK(!view.winner);
    }
  }
  HCR_CHECK(saw_stale_live);

  // Publisher liveness is not restored.
  std::vector<PublisherRecord> publishers;
  HCR_REQUIRE(client_two->list_publishers(publishers).ok());
  for (const PublisherRecord& record : publishers) {
    HCR_CHECK(record.state != PublisherState::Registered);
  }

  // Old-epoch traffic is refused.
  {
    RawConnection raw;
    HCR_REQUIRE(raw.open(port));
    HCR_REQUIRE(raw.handshake().ok());
    Publication stale;
    stale.kind = PublicationKind::PublishCapability;
    stale.publisher.id = PublisherId::parse("publisher.contradiction");
    stale.publisher.boot = PublisherBootId::first();
    stale.publisher.epoch = CoordinatorEpoch{first_epoch};
    stale.sequence = 5000;
    stale.evidence_generation = EvidenceGeneration{5000};
    stale.capability.device = canonical_device_id("dev.synthetic", "a100-conflict");
    stale.capability.device_generation = DeviceGeneration::first();
    stale.capability.device_boot = DeviceBootId::first();
    stale.capability.capability = CapabilityId::parse("cap.compute.fp8");
    stale.capability.schema = CapabilitySchemaId::parse("cap.compute.fp8.v1");
    stale.capability.support = SupportState::SupportedNative;
    stale.capability.value = CapabilityValue(true);
    stale.capability.source.source_class = SourceClass::SyntheticBackend;
    stale.capability.source.provenance = ProvenanceClass::Synthetic;
    stale.capability.source.adapter = "replay";
    ApplyCode code = ApplyCode::Accepted;
    std::string detail;
    HCR_REQUIRE(raw.send_publication(stale, code, detail).ok());
    HCR_CHECK_MSG(!apply_code_accepted(code), detail);
  }

  // Republishing with a fresh boot restores live authority.
  ChildProcess republisher;
  std::string republisher_output;
  HCR_REQUIRE(start_publisher(republisher, port, "publisher.contradiction", "contradiction-scenario",
                              republisher_output));
  const ResolvedCapability republished = query_or_fail(*client_two, "a100-conflict", "cap.compute.fp8");
  HCR_CHECK(republished.contradiction == ContradictionState::PreferredLiveEvidence);
  HCR_CHECK(republished.declared_support == SupportState::Unsupported);

  HCR_CHECK(client_two->shutdown("done").ok());
  unsigned long restarted_exit = 0;
  HCR_REQUIRE(restarted.wait_for_exit(restarted_exit));
  HCR_CHECK_EQ(restarted_exit, 0ul);
  republisher.terminate();
  remove_file(state_path);
#endif
}

HCR_TEST(distributed, repeated_lifecycle_leaves_no_orphans_and_no_stale_authority) {
#if !defined(_WIN32)
  HCR_SKIP("distributed proofs require the Windows process and socket implementation");
#else
  if (cli_path().empty()) HCR_SKIP("the hcr command line tool was not built");
  for (int round = 0; round < 3; ++round) {
    const std::uint16_t port = free_loopback_port();
    HCR_REQUIRE(port != 0);
    ChildProcess coordinator;
    std::string output;
    std::uint64_t epoch = 0;
    HCR_REQUIRE_MSG(start_coordinator(coordinator, port, "", output, epoch),
                    "coordinator did not start: " + output);
    auto client = connect_client(port);
    HCR_REQUIRE(client != nullptr);
    std::vector<SubjectSnapshot> subjects;
    HCR_REQUIRE(client->list_devices(subjects).ok());
    HCR_CHECK(subjects.empty());
    HCR_CHECK(client->shutdown("round complete").ok());
    unsigned long exit_code = 0;
    HCR_REQUIRE(coordinator.wait_for_exit(exit_code));
    HCR_CHECK_EQ(exit_code, 0ul);
  }
#endif
}
