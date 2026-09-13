// Hardware Capability Registry - real CUDA hardware validation.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// This program proves, on the machine it runs on, that the device actually
// executes code: it allocates device memory, copies host data to it, runs a
// real kernel, copies results back, checks them against a host-side parity
// computation and frees every allocation. Only the facts this program actually
// proved are published, with REAL_RUNTIME_PROBE provenance and PROBED
// precision. It deliberately does not claim any capability it did not exercise.
#include <cuda.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "hcr/canonical.hpp"
#include "hcr/discovery.hpp"
#include "hcr/registry.hpp"
#include "hcr/render.hpp"
#include "hcr/time.hpp"
#include "hcr/version.hpp"

namespace {

constexpr int kElements = 1 << 20;

__global__ void saxpy_kernel(const float* a, const float* b, float* out, float scale, int count) {
  const int index = blockIdx.x * blockDim.x + threadIdx.x;
  if (index < count) out[index] = a[index] * scale + b[index];
}

std::string format_uuid(const CUuuid& uuid) {
  static const char kDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(32);
  for (int i = 0; i < 16; ++i) {
    const unsigned char byte = static_cast<unsigned char>(uuid.bytes[i]);
    hex.push_back(kDigits[(byte >> 4) & 0x0fu]);
    hex.push_back(kDigits[byte & 0x0fu]);
  }
  return "GPU-" + hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
         hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

template <typename T>
bool check(T result, const char* what) {
  if (result == static_cast<T>(0)) return true;
  std::printf("FAIL %s -> code %d\n", what, static_cast<int>(result));
  return false;
}

}  // namespace

int main() {
  std::printf("Hardware Capability Registry %s real CUDA validation\n", hcr::version_string());

  int device_count = 0;
  if (!check(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount")) return 1;
  if (device_count <= 0) {
    std::printf("no CUDA device present; nothing to validate\n");
    return 1;
  }
  if (!check(cudaSetDevice(0), "cudaSetDevice")) return 1;

  cudaDeviceProp properties{};
  if (!check(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties")) return 1;

  CUuuid uuid{};
  CUdevice driver_device = 0;
  const bool have_driver_uuid = check(cuInit(0), "cuInit") &&
                                check(cuDeviceGet(&driver_device, 0), "cuDeviceGet") &&
                                check(cuDeviceGetUuid(&uuid, driver_device), "cuDeviceGetUuid");

  const std::size_t bytes = static_cast<std::size_t>(kElements) * sizeof(float);
  std::vector<float> host_a(kElements);
  std::vector<float> host_b(kElements);
  std::vector<float> host_out(kElements, 0.0f);
  for (int i = 0; i < kElements; ++i) {
    host_a[static_cast<std::size_t>(i)] = static_cast<float>(i % 97) * 0.5f;
    host_b[static_cast<std::size_t>(i)] = static_cast<float>(i % 13) * 0.25f;
  }
  constexpr float kScale = 2.5f;

  float* device_a = nullptr;
  float* device_b = nullptr;
  float* device_out = nullptr;
  bool ok = true;
  ok = ok && check(cudaMalloc(reinterpret_cast<void**>(&device_a), bytes), "cudaMalloc(a)");
  ok = ok && check(cudaMalloc(reinterpret_cast<void**>(&device_b), bytes), "cudaMalloc(b)");
  ok = ok && check(cudaMalloc(reinterpret_cast<void**>(&device_out), bytes), "cudaMalloc(out)");
  if (!ok) {
    if (device_a != nullptr) cudaFree(device_a);
    if (device_b != nullptr) cudaFree(device_b);
    if (device_out != nullptr) cudaFree(device_out);
    std::printf("RESULT cuda_validation=FAIL stage=allocation\n");
    return 1;
  }

  ok = ok && check(cudaMemcpy(device_a, host_a.data(), bytes, cudaMemcpyHostToDevice), "memcpy H2D a");
  ok = ok && check(cudaMemcpy(device_b, host_b.data(), bytes, cudaMemcpyHostToDevice), "memcpy H2D b");

  const int threads = 256;
  const int blocks = (kElements + threads - 1) / threads;
  if (ok) {
    saxpy_kernel<<<blocks, threads>>>(device_a, device_b, device_out, kScale, kElements);
    ok = ok && check(cudaGetLastError(), "kernel launch");
    ok = ok && check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
  }
  ok = ok && check(cudaMemcpy(host_out.data(), device_out, bytes, cudaMemcpyDeviceToHost), "memcpy D2H");

  std::size_t mismatches = 0;
  if (ok) {
    for (int i = 0; i < kElements; ++i) {
      const float expected = host_a[static_cast<std::size_t>(i)] * kScale + host_b[static_cast<std::size_t>(i)];
      if (host_out[static_cast<std::size_t>(i)] != expected) ++mismatches;
    }
  }

  std::size_t free_bytes = 0;
  std::size_t total_bytes = 0;
  const bool have_memory_info = check(cudaMemGetInfo(&free_bytes, &total_bytes), "cudaMemGetInfo");

  cudaFree(device_a);
  cudaFree(device_b);
  cudaFree(device_out);
  cudaDeviceSynchronize();
  cudaError_t cleanup = cudaGetLastError();
  if (cleanup != cudaSuccess) {
    std::printf("cleanup reported %s\n", cudaGetErrorString(cleanup));
    ok = false;
  }

  std::printf("device=%s compute_capability=%d.%d multiprocessors=%d global_memory=%zu\n",
              properties.name, properties.major, properties.minor, properties.multiProcessorCount,
              static_cast<std::size_t>(properties.totalGlobalMem));
  std::printf("executed saxpy elements=%d blocks=%d threads=%d mismatches=%zu\n", kElements, blocks, threads,
              mismatches);

  const bool executed = ok && mismatches == 0;
  if (!executed) {
    std::printf("RESULT cuda_validation=FAIL stage=execution\n");
    return 1;
  }

  // Publish exactly what was proved, through the production ingestion path.
  hcr::CapabilityRegistry registry;
  hcr::LocalEvidenceSink sink(registry);
  hcr::DiscoveryContext context;
  context.publisher.id = hcr::PublisherId::parse("publisher.cuda-validation");
  context.observed_at_utc = hcr::utc_now_iso8601();
  context.platform.id = hcr::PlatformId::parse("platform.host.local");
  context.platform.display_name = "local host";
  context.platform.vendor = "unknown";
  context.platform.model = "unknown";
  context.platform.architecture = hcr::ArchitectureId::parse("arch.x86_64");
  context.software.driver.id = hcr::DriverId::parse("driver.nvidia");
  context.software.runtime.id = hcr::RuntimeId::parse("runtime.cuda");
  context.source_class = hcr::SourceClass::HardwareProbe;
  context.provenance = hcr::ProvenanceClass::RealRuntimeProbe;
  context.adapter = "cuda.validation";

  const hcr::ApplyResult publisher = registry.register_publisher(context.publisher, "cuda.validation");
  if (!publisher.accepted()) {
    std::printf("publisher registration failed: %s\n", publisher.detail.c_str());
    return 1;
  }
  context.publisher = publisher.assigned_publisher;

  hcr::PublicationBuilder builder(sink, context);
  hcr::DeviceIdentity identity;
  const std::string uuid_text = have_driver_uuid ? format_uuid(uuid) : std::string();
  const std::string token = uuid_text.empty() ? std::string(properties.name) : uuid_text;
  identity.id = hcr::canonical_device_id("dev.gpu", hcr::ascii_lower(token));
  identity.incarnation = hcr::DeviceIncarnationId::parse("inc.gpu." + hcr::hex64(hcr::fnv1a64(token)));
  identity.hardware.vendor = hcr::VendorId::parse("vendor.nvidia");
  identity.hardware.product =
      hcr::ProductId::parse("product.nvidia." + hcr::ascii_lower(properties.name));
  identity.hardware.family = hcr::HardwareFamilyId::parse("family.nvidia.cuda-device");
  identity.hardware.model = hcr::HardwareModelId::parse("model.nvidia." + hcr::ascii_lower(properties.name));
  identity.hardware.revision = hcr::HardwareRevisionId::parse("rev.nvidia.cuda-validation");
  identity.hardware.architecture = hcr::ArchitectureId::parse(
      "arch.nvidia.sm_" + std::to_string(properties.major * 10 + properties.minor));
  identity.hardware.hardware_class = hcr::HardwareClass::Gpu;
  identity.hardware.native_identifiers = {{"cuda.uuid", uuid_text},
                                          {"cuda.name", properties.name},
                                          {"cuda.executed_workload", "saxpy-fp32"}};
  identity.platform = context.platform.id;
  identity.display_name = properties.name;

  hcr::DeviceRegistration registration;
  registration.identity = identity;
  registration.software = context.software;
  registration.platform = context.platform;
  registration.observed_at_utc = context.observed_at_utc;
  hcr::SubjectContext subject;
  const hcr::Status registered = builder.register_subject(registration, subject);
  if (!registered.ok()) {
    std::printf("device registration failed: %s\n", registered.describe().c_str());
    return 1;
  }

  const hcr::Status fp32 = builder.publish_capability(
      subject, hcr::CapabilityId::parse("cap.compute.fp32"), hcr::SupportState::SupportedNative,
      hcr::CapabilityValue(true), hcr::PrecisionClass::Probed,
      "executed fp32 saxpy kernel with host parity check");
  const hcr::Status arch = builder.publish_capability(
      subject, hcr::CapabilityId::parse("cap.runtime.kernel_architectures"),
      hcr::SupportState::SupportedNative,
      hcr::CapabilityValue(hcr::ArchitectureSetValue{
          {hcr::ArchitectureId::parse("arch.nvidia.sm_" +
                                      std::to_string(properties.major * 10 + properties.minor))}}),
      hcr::PrecisionClass::Probed, "compiled and executed a kernel for this architecture");
  const hcr::Status alloc = builder.publish_capability(
      subject, hcr::CapabilityId::parse("cap.runtime.runtime_api"), hcr::SupportState::SupportedNative,
      hcr::CapabilityValue(true), hcr::PrecisionClass::Probed,
      "cudaMalloc/cudaMemcpy/kernel launch/cudaFree all succeeded");
  (void)fp32;
  (void)arch;
  (void)alloc;
  if (have_memory_info) {
    const hcr::Status memory = builder.publish_capability(
        subject, hcr::CapabilityId::parse("cap.memory.capacity"), hcr::SupportState::SupportedNative,
        hcr::CapabilityValue(hcr::BytesValue{static_cast<std::uint64_t>(total_bytes)}),
        hcr::PrecisionClass::DirectReported, "cudaMemGetInfo");
    (void)memory;
  }

  hcr::CapabilityQuery query;
  query.device = identity.id;
  query.capability = hcr::CapabilityId::parse("cap.compute.fp32");
  hcr::ResolvedCapability resolved;
  const hcr::Status answered = registry.query_capability(query, resolved);
  if (!answered.ok()) {
    std::printf("query failed: %s\n", answered.describe().c_str());
    return 1;
  }

  hcr::DiscoveryReport report = builder.report();
  std::printf("published accepted=%zu idempotent=%zu rejected=%zu\n", report.accepted, report.idempotent,
              report.rejected);
  for (const std::string& note : report.notes) std::printf("note: %s\n", note.c_str());
  std::printf("%s\n", hcr::render_capability(resolved).c_str());
  std::printf("proves: fp32 device execution, kernel launch for this architecture, device memory "
              "allocation and host/device copies on device 0\n");
  std::printf("does not prove: other precisions, peer access, partitioning, networking paths, telemetry "
              "or any capability that was not exercised\n");
  std::printf("RESULT cuda_validation=PASS device=%s mismatches=%zu effective=%s\n", properties.name,
              mismatches, hcr::to_string(resolved.effective_support));
  return 0;
}
