// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/persistence.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "hcr/canonical.hpp"
#include "hcr/detail/codec.hpp"
#include "hcr/protocol.hpp"
#include "hcr/render.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hcr {
namespace {

constexpr std::uint32_t kStateMagic = 0x53524348u;  // 'HCRS' little-endian
constexpr std::size_t kStatePrefixSize = 24;

void write_publisher_watermark(detail::ByteWriter& writer, const PublisherWatermark& watermark,
                               const RegistryLimits& limits) {
  detail::write_publisher_identity(writer, PublisherIdentity{watermark.publisher, watermark.boot,
                                                              CoordinatorEpoch{}});
  writer.u64(watermark.max_sequence);
  writer.u64(watermark.max_evidence_generation.value());
  writer.u16(static_cast<std::uint16_t>(watermark.state));
  writer.str(watermark.adapter, limits.max_metadata_bytes);
  writer.str(watermark.reason, limits.max_metadata_bytes);
  writer.u64(watermark.accepted);
  writer.u64(watermark.rejected);
  writer.str(watermark.registered_at_utc, limits.max_metadata_bytes);
}

PublisherWatermark read_publisher_watermark(detail::ByteReader& reader, const RegistryLimits& limits) {
  PublisherWatermark watermark;
  const PublisherIdentity identity = detail::read_publisher_identity(reader);
  watermark.publisher = identity.id;
  watermark.boot = identity.boot;
  watermark.max_sequence = reader.u64();
  watermark.max_evidence_generation = EvidenceGeneration{reader.u64()};
  const std::uint16_t state = reader.u16();
  if (state > static_cast<std::uint16_t>(PublisherState::Dead)) {
    reader.fail(ErrorCode::CorruptData, "unknown publisher state in durable state");
    return watermark;
  }
  watermark.state = static_cast<PublisherState>(state);
  watermark.adapter = reader.str(limits.max_metadata_bytes);
  watermark.reason = reader.str(limits.max_metadata_bytes);
  watermark.accepted = static_cast<std::size_t>(reader.u64());
  watermark.rejected = static_cast<std::size_t>(reader.u64());
  watermark.registered_at_utc = reader.str(limits.max_metadata_bytes);
  return watermark;
}

void write_count(detail::ByteWriter& writer, std::size_t count, std::size_t bound) {
  writer.u32(static_cast<std::uint32_t>(count > bound ? bound : count));
}

}  // namespace

Status encode_durable_state(const DurableState& state, const PersistOptions& options,
                            std::vector<std::uint8_t>& out) {
  const RegistryLimits& limits = options.limits;
  std::vector<std::uint8_t> payload;
  detail::ByteWriter writer(payload);
  writer.u32(state.header.format_version);
  writer.u64(state.header.epoch.value());
  writer.u64(state.header.snapshot_generation.value());
  writer.u64(state.header.state_revision);
  writer.str(state.header.created_at_utc, limits.max_metadata_bytes);
  writer.str(state.header.producer, limits.max_metadata_bytes);

  write_count(writer, state.platforms.size(), limits.max_devices);
  for (std::size_t i = 0; i < state.platforms.size() && i < limits.max_devices; ++i) {
    detail::write_platform_identity(writer, state.platforms[i], limits);
  }
  write_count(writer, state.subjects.size(), limits.max_devices);
  for (std::size_t i = 0; i < state.subjects.size() && i < limits.max_devices; ++i) {
    detail::write_subject_snapshot(writer, state.subjects[i], limits);
  }
  write_count(writer, state.schemas.size(), limits.max_families * 16u);
  for (std::size_t i = 0; i < state.schemas.size() && i < limits.max_families * 16u; ++i) {
    detail::write_capability_schema(writer, state.schemas[i], limits);
  }
  write_count(writer, state.capability_history.size(), limits.max_evidence_history);
  for (std::size_t i = 0; i < state.capability_history.size() && i < limits.max_evidence_history; ++i) {
    detail::write_capability_record(writer, state.capability_history[i], limits);
  }
  write_count(writer, state.quirks.size(), limits.max_quirks);
  for (std::size_t i = 0; i < state.quirks.size() && i < limits.max_quirks; ++i) {
    detail::write_quirk_record(writer, state.quirks[i], limits);
  }
  write_count(writer, state.compatibility.size(), limits.max_compatibility_facts);
  for (std::size_t i = 0; i < state.compatibility.size() && i < limits.max_compatibility_facts; ++i) {
    detail::write_compatibility_fact(writer, state.compatibility[i], limits);
  }
  write_count(writer, state.publisher_watermarks.size(), limits.max_publishers * 64u);
  for (std::size_t i = 0; i < state.publisher_watermarks.size() && i < limits.max_publishers * 64u; ++i) {
    write_publisher_watermark(writer, state.publisher_watermarks[i], limits);
  }
  write_count(writer, state.fenced_boots.size(), limits.max_publishers * 64u);
  for (std::size_t i = 0; i < state.fenced_boots.size() && i < limits.max_publishers * 64u; ++i) {
    write_publisher_watermark(writer, state.fenced_boots[i], limits);
  }
  writer.u64(state.canonical_digest);

  if (writer.truncated()) {
    return Status::failure(ErrorCode::LimitExceeded,
                           "durable state carries a field longer than the configured bound");
  }
  const std::size_t max_bytes = options.max_bytes == 0 ? limits.max_persistence_bytes : options.max_bytes;
  if (payload.size() + kStatePrefixSize > max_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "encoded durable state exceeds the configured byte bound");
  }

  out.clear();
  out.reserve(payload.size() + kStatePrefixSize);
  detail::ByteWriter header(out);
  header.u32(kStateMagic);
  header.u16(static_cast<std::uint16_t>(state_format_version()));
  header.u16(0u);
  header.u32(static_cast<std::uint32_t>(payload.size()));
  header.u32(crc32(payload.data(), payload.size()));
  header.u64(state.canonical_digest);
  out.insert(out.end(), payload.begin(), payload.end());
  return Status::success();
}

Status decode_durable_state(const std::vector<std::uint8_t>& bytes, const PersistOptions& options,
                            DurableState& out) {
  const RegistryLimits& limits = options.limits;
  if (bytes.size() < kStatePrefixSize) {
    return Status::failure(ErrorCode::CorruptData, "durable state image is shorter than its prefix");
  }
  detail::ByteReader header(bytes.data(), kStatePrefixSize);
  const std::uint32_t magic = header.u32();
  if (magic != kStateMagic) {
    return Status::failure(ErrorCode::CorruptData, "durable state magic does not match");
  }
  const std::uint16_t version = header.u16();
  if (version != static_cast<std::uint16_t>(state_format_version())) {
    return Status::failure(ErrorCode::UnsupportedVersion,
                           "durable state format version " + std::to_string(version) + " is not supported");
  }
  const std::uint16_t flags = header.u16();
  if (flags != 0u) {
    return Status::failure(ErrorCode::CorruptData, "durable state declares unknown flags");
  }
  const std::uint32_t payload_length = header.u32();
  const std::uint32_t checksum = header.u32();
  const std::uint64_t digest = header.u64();
  if (static_cast<std::size_t>(payload_length) != bytes.size() - kStatePrefixSize) {
    return Status::failure(ErrorCode::CorruptData, "durable state payload length does not match the image");
  }
  const std::uint8_t* payload = bytes.data() + kStatePrefixSize;
  if (crc32(payload, payload_length) != checksum) {
    return Status::failure(ErrorCode::IntegrityFailure, "durable state checksum does not match");
  }
  const std::size_t max_bytes = options.max_bytes == 0 ? limits.max_persistence_bytes : options.max_bytes;
  if (bytes.size() > max_bytes) {
    return Status::failure(ErrorCode::LimitExceeded, "durable state exceeds the configured byte bound");
  }

  DurableState state;
  detail::ByteReader reader(payload, payload_length);
  state.header.format_version = reader.u32();
  state.header.epoch = CoordinatorEpoch{reader.u64()};
  state.header.snapshot_generation = SnapshotGeneration{reader.u64()};
  state.header.state_revision = reader.u64();
  state.header.created_at_utc = reader.str(limits.max_metadata_bytes);
  state.header.producer = reader.str(limits.max_metadata_bytes);

  const auto read_count = [&reader, &limits](std::size_t bound, const char* what) -> std::uint32_t {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) return 0;
    if (count > bound || count > reader.remaining()) {
      reader.fail(ErrorCode::LimitExceeded, std::string("durable state ") + what + " count exceeds the bound");
      return 0;
    }
    return count;
  };

  const std::uint32_t platforms = read_count(limits.max_devices, "platform");
  for (std::uint32_t i = 0; i < platforms && reader.ok(); ++i) {
    state.platforms.push_back(detail::read_platform_identity(reader, limits));
  }
  const std::uint32_t subjects = read_count(limits.max_devices, "subject");
  for (std::uint32_t i = 0; i < subjects && reader.ok(); ++i) {
    state.subjects.push_back(detail::read_subject_snapshot(reader, limits));
  }
  const std::uint32_t schemas = read_count(limits.max_families * 16u, "schema");
  for (std::uint32_t i = 0; i < schemas && reader.ok(); ++i) {
    state.schemas.push_back(detail::read_capability_schema(reader, limits));
  }
  const std::uint32_t records = read_count(limits.max_evidence_history, "evidence record");
  for (std::uint32_t i = 0; i < records && reader.ok(); ++i) {
    state.capability_history.push_back(detail::read_capability_record(reader, limits));
  }
  const std::uint32_t quirks = read_count(limits.max_quirks, "quirk");
  for (std::uint32_t i = 0; i < quirks && reader.ok(); ++i) {
    state.quirks.push_back(detail::read_quirk_record(reader, limits));
  }
  const std::uint32_t compatibility = read_count(limits.max_compatibility_facts, "compatibility fact");
  for (std::uint32_t i = 0; i < compatibility && reader.ok(); ++i) {
    state.compatibility.push_back(detail::read_compatibility_fact(reader, limits));
  }
  const std::uint32_t watermarks = read_count(limits.max_publishers * 64u, "publisher watermark");
  for (std::uint32_t i = 0; i < watermarks && reader.ok(); ++i) {
    state.publisher_watermarks.push_back(read_publisher_watermark(reader, limits));
  }
  const std::uint32_t fenced = read_count(limits.max_publishers * 64u, "fenced boot");
  for (std::uint32_t i = 0; i < fenced && reader.ok(); ++i) {
    state.fenced_boots.push_back(read_publisher_watermark(reader, limits));
  }
  state.canonical_digest = reader.u64();

  if (!reader.ok()) return reader.status();
  if (!reader.exhausted()) {
    return Status::failure(ErrorCode::CorruptData, "durable state image carries trailing bytes");
  }
  if (state.header.format_version != state_format_version()) {
    return Status::failure(ErrorCode::UnsupportedVersion, "durable state header format version mismatch");
  }
  if (digest != 0 && state.canonical_digest != digest) {
    return Status::failure(ErrorCode::CorruptData, "durable state digest header does not match its payload");
  }
  out = std::move(state);
  return Status::success();
}

StateStore::~StateStore() = default;

FileStateStore::FileStateStore(std::string path, PersistOptions options)
    : path_(std::move(path)), options_(options) {}

FileStateStore::~FileStateStore() = default;

Status FileStateStore::save(const DurableState& state) {
  std::vector<std::uint8_t> bytes;
  const Status encoded = encode_durable_state(state, options_, bytes);
  if (!encoded.ok()) return encoded;
  const std::string temporary = path_ + ".tmp";
  {
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) {
      return Status::failure(ErrorCode::IoFailure, "cannot open temporary state file " + temporary);
    }
    const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const int flushed = std::fflush(file);
    const int closed = std::fclose(file);
    if (written != bytes.size() || flushed != 0 || closed != 0) {
      std::remove(temporary.c_str());
      return Status::failure(ErrorCode::IoFailure, "failed to write temporary state file");
    }
  }
#if defined(_WIN32)
  if (!MoveFileExA(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::remove(temporary.c_str());
    return Status::failure(ErrorCode::IoFailure, "atomic replacement of " + path_ + " failed");
  }
#else
  if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
    std::remove(temporary.c_str());
    return Status::failure(ErrorCode::IoFailure, "atomic replacement of " + path_ + " failed");
  }
#endif
  return Status::success();
}

Status FileStateStore::load(DurableState& out) {
  std::FILE* file = std::fopen(path_.c_str(), "rb");
  if (file == nullptr) {
    return Status::failure(ErrorCode::IoFailure, "cannot open state file " + path_);
  }
  std::vector<std::uint8_t> bytes;
  std::uint8_t buffer[65536];
  std::size_t read = 0;
  const std::size_t max_bytes = options_.max_bytes == 0 ? options_.limits.max_persistence_bytes
                                                        : options_.max_bytes;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    if (bytes.size() + read > max_bytes) {
      std::fclose(file);
      return Status::failure(ErrorCode::LimitExceeded, "state file exceeds the configured byte bound");
    }
    bytes.insert(bytes.end(), buffer, buffer + read);
  }
  const bool read_error = std::ferror(file) != 0;
  std::fclose(file);
  if (read_error) {
    return Status::failure(ErrorCode::IoFailure, "failed while reading state file " + path_);
  }
  return decode_durable_state(bytes, options_, out);
}

bool FileStateStore::exists() const {
  std::FILE* file = std::fopen(path_.c_str(), "rb");
  if (file == nullptr) return false;
  std::fclose(file);
  return true;
}

std::string FileStateStore::describe() const { return "file:" + path_; }

}  // namespace hcr
