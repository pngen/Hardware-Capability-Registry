# Hardware Capability Registry

**Canonical, versioned, provenance-preserving hardware capability knowledge for heterogeneous AI infrastructure.**

Hardware Capability Registry (HCR) is a C++20 runtime and installable library that answers one
systems question:

> **What hardware and platform capabilities are known for this exact hardware identity and
> generation now, what evidence supports those facts, which facts are current, which are vendor- or
> environment-specific, which are inferred or synthetic, which remain unknown or unsupported, and
> which downstream systems may safely rely on them?**

It exists to keep two statements apart that are routinely conflated:

* *A device was discovered.*
* *This exact device identity and generation has these exact current capabilities under this driver,
  firmware, topology, runtime and evidence generation, with explicit provenance and compatibility
  facts.*

and two more:

* *A capability was once observed.*
* *This capability remains current and authoritative for this exact hardware generation and has not
  been superseded, contradicted, fenced, invalidated or rendered stale by device, firmware, driver,
  runtime, topology or publisher change.*

Version 1.0.0 is closed against real hardware: an NVIDIA RTX 5090 on driver 616.92, an AMD Ryzen 7
9800X3D platform, real PCI topology, real network interfaces, and honest reporting of what is absent
(no CXL device, no MIG support on this part, no ECC exposure).

---

## 1. What it is

* A **knowledge boundary**, not a scheduler. Downstream systems ask it what hardware can do; they
  decide what to do about it.
* **Typed and versioned**: capabilities are declared by versioned schemas with typed value forms
  (boolean, count, bytes, rate, bandwidth, latency class, enumeration, feature set, version range,
  architecture set, numeric range, structured record, capability relationship, predicate). There is
  no untyped string-to-string capability bag in the model.
* **Generation-bound**: hardware identity, device incarnation, firmware, driver, runtime, platform,
  capability, quirk, compatibility, evidence and snapshot generations are real boundaries that fence
  stale knowledge.
* **Provenance-preserving**: every statement carries its publisher, boot identity, coordinator
  epoch, source class, provenance class, precision, native API reference and observation sequence.
* **Contradiction-aware**: disagreeing evidence is retained and resolved by documented precedence,
  never silently dropped.
* **Quirk-aware**: quirks are first-class records that override advertised support deterministically.
* **Honest**: UNKNOWN, UNSUPPORTED, DISABLED, EMULATED, CONDITIONAL, SYNTHETIC and REVALIDATION_REQUIRED
  are distinct, and a capability that was never observed is never reported as supported.

## 2. Exact systems boundary

**HCR owns**: canonical hardware identity; hardware family and generation; device generation and
incarnation; vendor, product/model, architecture, revision/stepping identity; firmware, driver and
runtime generation; capability identity, generation, schema, value, scope, provenance, freshness,
confidence and precision; contradiction handling; supersession; quirk identity, generation and
applicability; compatibility facts and their generations; feature support, absence, UNKNOWN,
UNSUPPORTED and conditional/environment/backend/platform-qualified support; hardware discovery
adapters; evidence publisher authority; durable canonical knowledge; conservative restart semantics;
deterministic queries and explanations; immutable snapshots; REAL / SYNTHETIC / UNSUPPORTED
classification; real hardware validation; distributed publisher/coordinator authority.

**HCR does not own**: hardware scheduling, workload placement, model routing, resource allocation,
accelerator partitioning, runtime evolution, model lifecycle, coherence policy, network routing,
memory placement, device health policy, failure recovery policy, driver implementation, firmware
management, kernel compilation, binary deployment, resource brokering, hardware control-plane
mutation, or generic inventory management unrelated to capability truth.

Adjacent systems keep their authority: a **Compatibility Registry** may govern generalized software
compatibility; a **Runtime Registry** may describe runtime services; **Accelerator Health** decides
whether a device is healthy; **Topology/Cluster/Rack Fabric** owns relationships between hardware
resources. HCR consumes or links that identity and supplies canonical capability knowledge in
return. HCR determines what a device is capable of, never whether it should be used.

## 3. Architecture

```
include/hcr/            public headers (installed)
  ids.hpp               canonical identity tokens and generation counters
  hardware.hpp          hardware classes, multi-level identity, software state
  capability.hpp        capability schemas and evidence records
  value.hpp             typed capability values
  condition.hpp         conditional predicates and environment evaluation
  support.hpp           support states, effective support, contradiction, precision
  provenance.hpp        source classes, provenance classes, publisher authority, staleness
  quirk.hpp             quirk records, applicability, precedence
  compatibility.hpp     hardware-centered compatibility facts
  resolution.hpp        resolved capability answers and factual comparisons
  query.hpp             query request types
  snapshot.hpp          immutable snapshots and publisher records
  publication.hpp       the single ingestion envelope and apply results
  registry.hpp          CapabilityRegistry: the public knowledge boundary
  durable_state.hpp     persistence model
  discovery.hpp         discovery adapters, evidence sinks, publication builder
  adapters.hpp          real adapters, host probe, synthetic profiles
  persistence.hpp       encoded state and atomic file store
  protocol.hpp          bounded framed protocol and payload codecs
  transport.hpp         blocking TCP transport
  coordinator.hpp       CapabilityCoordinator
  publisher.hpp         CapabilityPublisher
  client.hpp            CapabilityClient
  render.hpp            deterministic rendering and canonical digests
  catalog.hpp           typed capability catalog
  limits.hpp            resource bounds
src/                    implementation (see below)
tests/                  hcr_tests, hcr_distributed_tests, hcr_hardware_tests
examples/               eleven focused examples plus an independent consumer
benchmarks/             measured operation benchmarks
```

Core components: `CapabilityRegistry` (identity, ingestion, resolution, snapshots, persistence
model), `CapabilityCoordinator` (publisher authority, coordinator epoch, durable state),
`CapabilityPublisher` and `CapabilityClient` (framed protocol over real sockets),
`DiscoveryBackend` adapters (CPU/platform, PCI, NVML, CUDA driver API, network, CXL, synthetic),
`EvidenceResolver` (contradiction and staleness resolution), the quirk engine and the compatibility
engine, and `hcr`, a read-only inspection CLI plus service host.

**Concurrency and lock ownership.** All registry methods take one mutex and never hold it across a
vendor API call, device probe, socket read/write, filesystem call, callback, thread join or process
wait. Discovery adapters gather facts outside the registry and submit immutable publications through
the single ingestion path. The coordinator event loop owns its session map and takes a lock only for
map lookups; statistics are relaxed atomics; socket I/O never happens under a lock. The global lock
order is: coordinator session map → registry state; no path takes the registry lock and then
requests a coordinator lock.

## 4. Hardware identity model

```
VendorId, ProductId, HardwareFamilyId, HardwareModelId, HardwareRevisionId,
ArchitectureId, PlatformId, DeviceId, DeviceIncarnationId
```

Identities are canonical opaque tokens (lowercase ASCII, digits, `.`, `_`, `:`, `-`, at most 128
bytes). Backend-native strings — NVML UUIDs, PCI bus addresses, SMBIOS strings, Windows adapter
GUIDs — are preserved verbatim as `native_identifiers` provenance metadata and never participate in
canonical identity. Serial numbers are optional: NVML reports none for a GeForce part, and the model
does not require one. Bus addresses are explicitly instance-local (`instance_locator`) and are never
used as globally stable identity.

Hardware classes: CPU, GPU, NPU, TPU-like accelerator, FPGA, ASIC accelerator, NIC, SmartNIC, DPU,
CXL device, memory expander, HBM domain, DRAM domain, persistent memory, PCIe switch,
NVSwitch-like switch, fabric switch, storage controller, NVMe device, network adapter, offload
engine, other. Classes share one identity core and keep class-specific capability namespaces.

Software state per device: firmware, driver and runtime identity with monotonic generations. A
registration that does not identify an axis does not erase what is already known about it.

## 5. Capability schema and catalog

The built-in catalog declares **110 typed, versioned schemas** (`cap.<domain>.<name>.v<N>`) across
compute, memory, partitioning, interconnect, networking, runtime, reliability, power and platform
domains. A publication must resolve to a known schema, and the registry binds the schema itself: a
publisher names the capability, the registry records which versioned schema it was interpreted
under, so a capability can never be silently reinterpreted. Values are canonicalized (set-like
values sorted and deduplicated) before validation and storage.

Each evidence record carries: device, device generation, device boot, capability, schema, capability
generation, support state, typed value, precision, provenance source (class, provenance, publisher,
boot, epoch, adapter, native API reference, observation sequence, observation time, evidence
generation), conditions, observed firmware/driver/runtime generation **and identity**, platform
generation, and withdrawal state.

## 6. Support states

`SUPPORTED_NATIVE`, `SUPPORTED_CONDITIONAL`, `SUPPORTED_EMULATED`, `SUPPORTED_SOFTWARE_ASSISTED`,
`SUPPORTED_EXPERIMENTAL`, `DISABLED`, `UNSUPPORTED`, `UNKNOWN`, `REVALIDATION_REQUIRED`.

A query returns both the **declared** state (what the evidence asserts) and the **effective** state
(after conditions, quirks and staleness): `SUPPORTED`, `SUPPORTED_CONDITIONAL_UNMET`,
`SUPPORTED_CONDITIONAL_UNKNOWN`, `SUPPORTED_WITH_CAVEAT`, `BLOCKED_BY_QUIRK`, `DISABLED`,
`UNSUPPORTED`, `UNKNOWN`, `REVALIDATION_REQUIRED`.

* **UNKNOWN is never SUPPORTED.** A capability with no evidence answers UNKNOWN and is explicitly
  not current.
* **UNSUPPORTED is positive evidence of non-support** for the applicable generation and context.
* **EMULATED is not NATIVE**, **DISABLED is not UNSUPPORTED**, **SYNTHETIC is not REAL**.

Real example from this machine:

```
cap.partition.mechanism = UNSUPPORTED   (nvmlDeviceGetMigMode -> NVML_ERROR_NOT_SUPPORTED)
cap.net.rdma            = UNKNOWN       (no adapter can prove RDMA through the OS APIs used)
```

## 7. Conditional capability

Conditions are predicate trees (`all_of`, `any_of`, `not`) over leaves for minimum driver, firmware,
runtime or platform generation, required OS, kernel version, device mode, topology, peer hardware,
partition state, platform/BIOS setting, privilege, device generation and feature gate. They are
evaluated against a caller-supplied `EnvironmentContext`; **absent facts evaluate to UNKNOWN, never
to satisfied.** A query returns the per-leaf evidence and one of satisfied / unsatisfied / unknown,
so "supported only if condition C holds" survives every layer of the model:

```
no environment supplied  -> SUPPORTED_CONDITIONAL_UNKNOWN
conditions satisfied     -> SUPPORTED
condition unmet          -> SUPPORTED_CONDITIONAL_UNMET
```

## 8. Quirks

A quirk binds an identity, applicability selectors (vendors, families, models, devices, revision
ranges, driver/firmware/runtime version ranges, platform conditions), impacted capabilities,
category, severity, mitigation, provenance and generation. Applicability is evaluated
deterministically: selectors that cannot be evaluated (for example an unknown device revision) yield
`unknown` applicability rather than a guess.

**Precedence is explicit.** Conditions qualify the declared support first; applicable quirks then
modify the qualified state. The most severe applicable quirk decides: `unusable`/`fatal` produce
`BLOCKED_BY_QUIRK`, `caveat`/`degraded` produce `SUPPORTED_WITH_CAVEAT`, informational quirks are
recorded and change nothing. Quirks are ordered by severity, then quirk id, then quirk generation,
so overlapping quirks resolve identically on every run. Both facts stay visible: the capability's
advertised support and the quirk that blocks it.

```
with defective driver: cap.interconnect.peer_to_peer = BLOCKED_BY_QUIRK [declared SUPPORTED_NATIVE]
after driver upgrade:  cap.interconnect.peer_to_peer = SUPPORTED        [generation 2]
```

## 9. Contradictory evidence

Evidence from different sources is retained per source slot (publisher boot × source class).
Contradiction states: `CONSISTENT`, `CONFLICTING_EVIDENCE`, `PREFERRED_LIVE_EVIDENCE`,
`PREFERRED_HIGHER_AUTHORITY_SOURCE`, `AMBIGUOUS`, `REVALIDATION_REQUIRED`.

Documented precedence: an actively executed, cleaned-up hardware probe outranks every passive query;
passive live queries (vendor API, driver, firmware, runtime) outrank enumeration; enumeration
outranks curated and imported knowledge; curated databases outrank imported manifests; the synthetic
backend never outranks a real source. Losers are preserved and reported with `[selected]` /
`[not-selected]` and a reason. When the top authority itself disagrees, the answer is `AMBIGUOUS`
with a deterministic reporting winner, never a silent choice.

```
cap.compute.fp8 = UNSUPPORTED [contradiction PREFERRED_LIVE_EVIDENCE]
  curated.architecture-table  CURATED_STATIC  SUPPORTED_NATIVE  [selected]      <- curated claim
  runtime.probe               REAL_RUNTIME_PROBE UNSUPPORTED    [not-selected]  <- live probe wins
```

## 10. Provenance and precision

Source classes: live vendor API, driver query, firmware query, runtime probe, hardware probe, OS
enumeration, PCI configuration, ACPI/SMBIOS, NVML, CUDA runtime, ROCm SMI, NIC driver API, CXL
enumeration, static curated database, imported manifest, synthetic backend.

Provenance classes: `REAL_LIVE_HARDWARE`, `REAL_OS_REPORTED`, `REAL_VENDOR_API`,
`REAL_RUNTIME_PROBE`, `CURATED_STATIC`, `DERIVED`, `IMPORTED`, `SYNTHETIC`, `UNKNOWN_SOURCE`.
A source may weaken its claim but never strengthen it: a curated database cannot publish live
device proof, an OS enumeration cannot claim an executed probe, and the synthetic backend cannot
claim anything real. Such publications are rejected before mutation.

Precision classes: `EXACT`, `PROBED`, `DIRECT_REPORTED`, `DERIVED`, `CURATED`, `INFERRED`,
`UNKNOWN`. Inferred family-wide knowledge can never masquerade as exact device evidence — the
curated datatype table for the RTX 5090 is reported with precision `CURATED`, while the FP32 kernel
that was actually executed is `PROBED`.

Durability is part of provenance: curated, imported and derived knowledge is durable across restarts;
everything produced by a live process (vendor API, enumeration, runtime or hardware probe, synthetic
backend) is process-bound and becomes `REVALIDATION_REQUIRED` after a restart.

## 11. Compatibility facts

Hardware-centered only: accelerator ↔ runtime, driver, compiler target, kernel architecture; NIC ↔
RDMA stack; GPU ↔ NIC direct path; CXL device ↔ platform; partition profile ↔ device; firmware ↔
driver; DPU service ↔ hardware generation; memory type ↔ execution engine. Outcomes are
`COMPATIBLE`, `COMPATIBLE_CONDITIONAL`, `INCOMPATIBLE`, `UNKNOWN`, `UNSUPPORTED`,
`REVALIDATION_REQUIRED`, always with an exact reason, optional conditions and a provenance source.
Device, family, model, architecture and platform references must resolve against registered
subjects; a dangling reference is rejected. Generalized software compatibility management belongs to
a compatibility registry, not here.

## 12. Device generations and staleness

A device has one current generation. `DeviceGeneration` advances on incarnation change (reset,
replacement, re-enumeration); `DeviceBootId` advances on every observed device boot. Firmware,
driver and runtime generations advance when the identified software state changes, and never
regress.

Evidence is qualified by the axis it was observed under:

* **reset / replacement** invalidates every retained capability record of the previous generation;
* **driver or firmware change** invalidates live evidence observed under an identified generation of
  that axis;
* **runtime change** invalidates evidence observed through *that same* runtime component, because
  several runtimes can expose one device;
* **platform change** invalidates platform-qualified live evidence;
* **withdrawal** retracts the withdrawing publisher's claim without asserting non-support.

A publication bound to a superseded generation, boot or capability generation is rejected before it
can mutate anything, and the resolved answer for a device whose evidence is no longer current is
`REVALIDATION_REQUIRED` with `current = false` — the retained assertion stays visible for audit and
is never presented as current.

## 13. Publisher authority

Every publication carries publisher id, publisher boot id, coordinator epoch, sequence and evidence
generation. The registry assigns boot identities: a publisher that connects with no boot receives a
fresh one, and **every earlier boot of that publisher is permanently fenced**. A dead publisher
loses authority for its boot identity forever; replay from it is refused, including after a
coordinator restart, because replay watermarks are durable. A replacement process receives a new
boot and must republish before its evidence becomes current again.

Ingestion rejects, before mutation: unknown or fenced publishers, stale boot, stale epoch, stale
sequence, conflicting duplicate sequences, evidence-generation regression, capability-generation
regression, stale device generation, unknown schema, unknown device, dangling compatibility
references, unknown impacted capabilities, and every limit violation.

## 14. Persistence and recovery

The state format is a versioned binary container: magic, format version, flags, payload length,
CRC-32 integrity check and a canonical content digest. Counts are bounded, strings are bounded,
generations are validated, records are canonically ordered and every reference is resolved
**before anything is applied**. A failed load leaves the registry untouched; a rejected save leaves
the previous file intact (temporary file plus atomic replacement).

After a restart:

* canonical identities, retained evidence history, quirks, compatibility facts, curated knowledge
  and replay watermarks are restored;
* the coordinator epoch is advanced, so old-epoch frames are refused;
* **publisher liveness is not restored** — every publisher must re-register with a fresh boot;
* process-bound evidence is `REVALIDATION_REQUIRED` rather than silently current;
* durable curated knowledge stays current;
* the current capability view is recomputed from current evidence.

## 15. Distributed mode

`CapabilityCoordinator`, `CapabilityPublisher` and `CapabilityClient` speak a bounded, versioned,
checksum-protected framed protocol over real TCP sockets: magic, protocol version, message type,
flags, bounded payload length, CRC-32, correlation id. Messages: `HELLO`, `WELCOME`,
`REGISTER_PUBLISHER`, `PUBLISHER_ACCEPTED`, `PUBLICATION`, `PUBLICATION_ACCEPTED`,
`QUERY_REQUEST`, `QUERY_RESPONSE`, `FENCE_PUBLISHER`, `FENCE_ACCEPTED`, `SNAPSHOT_REQUEST`,
`SNAPSHOT_RESPONSE`, `SHUTDOWN_REQUEST`, `SHUTDOWN_ACCEPTED`, `ERROR_RESPONSE`, `HEARTBEAT`,
`HEARTBEAT_ACCEPTED`. Malformed frames are rejected before they can mutate anything: empty or
truncated input, wrong magic, unsupported version, unknown type, undefined flags, oversized length,
corrupt checksum, trailing bytes, unknown enumerations, oversized fields.

The coordinator processes **death before interpretation**: in one event-loop iteration every ready
session is read first, dead sessions are retired, and only then are frames dispatched. The logical
outcome therefore does not depend on the order in which the kernel reports readiness.

No timeouts are configured anywhere — blocking accepts, blocking reads, an indefinite
`select`. A hang is a defect, not something the transport papers over. Shutdown stops accepting
publishers, fences every remaining session, persists committed state, closes sockets and leaves no
orphan processes.

## 16. Hardware discovery adapters

* **cpu.platform** — CPUID (vendor, brand, family/model/stepping, ISA feature bits, XCR0-gated AVX
  and AVX-512 usability), `GetLogicalProcessorInformationEx` topology (cores, NUMA, cache) and
  `GetActiveProcessorCount`, page sizes, SMBIOS/BIOS identity through the registry, OS/kernel build
  through `RtlGetVersion`.
* **pci.topology** — SetupAPI enumeration of present PCI functions and their class codes.
* **nvml** — dynamically loaded NVML: name, UUID, PCI identity, memory, memory bus width, driver
  version, VBIOS, compute capability, core count, MIG mode, ECC mode, power-cap range, thermal
  sensors, and the telemetry surfaces that actually answered. No build-time or install-time NVIDIA
  dependency exists: the library loads `nvml.dll` at run time and reports the adapter as unavailable
  when it is absent.
* **cuda.driver-api** — dynamically loaded `nvcuda`: driver API version, per-device UUID, compute
  capability, multiprocessor count, unified addressing, host-memory mapping, concurrent kernels,
  cooperative launch, managed memory; plus a deliberately conservative **curated** datatype table
  published with `CURATED_STATIC` provenance and `CURATED` precision.
* **nic.windows** — `GetAdaptersAddresses`: interface state, link speed, MTU, bus-mastering. RDMA,
  SR-IOV, offload and SmartNIC capabilities are **not** discoverable through this API and are
  therefore left UNKNOWN rather than guessed.
* **cxl.enumeration** — CXL-class PCI function scan; on a platform without one it publishes the count
  (0) and positive non-support for CXL memory exposure.
* **synthetic.backend** — eleven deterministic profiles (mixed vendor fleet, generation drift, AMD
  fleet, CXL fabric, DPU/SmartNIC fabric, partitioning profiles, contradiction, quirk, unsupported,
  reset, precision matrix) that publish through the same production pipeline and are always
  classified SYNTHETIC.

An adapter cannot strengthen provenance: the registry rejects a claim stronger than the source class
implies.

## 17. REAL / SYNTHETIC / UNSUPPORTED

Every hardware-facing statement is classified:

* **REAL** — an observation of actual hardware through an actual API on this machine.
* **SYNTHETIC** — hardware that is not physically present, simulated through the production pipeline.
* **UNSUPPORTED** — a capability positively shown unavailable, or a device class that is absent with
  no claim possible.
* **UNKNOWN** — no evidence exists; this is not a classification of absence but of ignorance.

SYNTHETIC never means UNKNOWN, and UNSUPPORTED is never inferred from missing evidence.

## 18. Real hardware validation on this machine

Discovery over a live coordinator with the real adapters published **72 statements with zero
rejections** across 9 subjects, producing a snapshot with 57 capability records: REAL 52,
UNSUPPORTED 5, UNKNOWN 0, stale evidence 0.

**GPU — NVIDIA GeForce RTX 5090**

```
identity      dev.gpu.e106289f5d0337c4 (vendor.nvidia, arch.nvidia.sm_120)
uuid          GPU-d1056bb6-4fec-2891-83f2-3a24fc70276b   pci 00000000:01:00.0
driver        616.92        vbios 98.02.2e.40.6a
compute       cap.compute.capability = compute_capability:12.0
units         cap.compute.execution_units = 21760 cores
              cap.compute.multiprocessors = 170 multiprocessors   (CUDA driver API)
memory        cap.memory.capacity = 34190917632 B   bus width 512 bits
mig           cap.partition.mechanism = UNSUPPORTED
              evidence nvml reference=nvmlDeviceGetMigMode -> NVML_ERROR_NOT_SUPPORTED
                                                        precision EXACT, truth UNSUPPORTED
ecc           cap.memory.ecc = UNSUPPORTED  (nvmlDeviceGetEccMode -> NOT_SUPPORTED)
              cap.reliability.ecc_exposure = UNSUPPORTED
power         cap.power.cap_range = [400000,600000] step 1 (mW)
datatypes     cap.compute.datatypes = {bf16,fp16,int4,int8,tf32}   precision CURATED
```

MIG is reported exactly as the hardware reports it: NVML answered `NOT_SUPPORTED` for this device,
so the registry publishes UNSUPPORTED for this exact device generation with `EXACT` precision and
does not infer family-wide MIG support from the product family. RDMA and peer-direct paths are never
claimed for this device.

**CUDA execution proof** (`hcr_cuda_validation`, built when a CUDA toolkit is present): device
memory allocation, host→device copy, a real SAXPY kernel over 1,048,576 elements in 4096 blocks,
device→host copy, host parity check with **0 mismatches**, cleanup with no residual error. The
program then publishes only what it proved, through the production pipeline, with
`REAL_RUNTIME_PROBE` provenance and `PROBED` precision:

```
capability cap.compute.fp32 on device dev.gpu.a024b16b62590f01 (generation 1, current)
  declared: SUPPORTED_NATIVE   effective: SUPPORTED   truth: REAL   precision: PROBED
  evidence cuda.validation reference=executed fp32 saxpy kernel with host parity check
RESULT cuda_validation=PASS device=NVIDIA GeForce RTX 5090 mismatches=0 effective=SUPPORTED
```

It explicitly does not claim other precisions, peer access, partitioning, networking paths or
telemetry.

**CPU / platform — AMD Ryzen 7 9800X3D**

```
cap.platform.cpu.physical_cores     = 8 cores          (EXACT)
cap.platform.cpu.logical_processors = 16 processors    (EXACT)
cap.memory.numa_nodes               = 1 node           (EXACT)
cap.platform.cache_hierarchy        = {l1d 393216 B, l1i 262144 B, l2 8388608 B, l3 100663296 B}
cap.platform.cpu.isa_features       = 29 features from CPUID and XCR0 (PROBED)
cap.platform.page_sizes             = {2m,4k}
cap.compute.architecture            = {arch.x86_64}
```

**Platform**: X870E AORUS MASTER, BIOS F14c — identity and firmware generation come from SMBIOS.
**PCI**: 63 present functions enumerated. **NICs**: 6 physical interfaces (Realtek PCIe 5GbE, Wi-Fi 7,
Bluetooth PAN) with real interface state, MTU and link speed. **CXL**: no CXL-class function is
present, so `cap.platform.cxl_devices = 0` and `cap.memory.cxl_exposure = UNSUPPORTED` — positive
evidence of absence, not a fabricated capability. **SmartNIC/DPU/RDMA** claims: none are made on this
platform; those capabilities answer UNKNOWN. Synthetic DPU, SmartNIC, CXL and RDMA cases are
available and are always classified SYNTHETIC.

## 19. Build

Requirements: CMake 3.20+, a C++20 compiler (MSVC 19.3x, GCC 11+, Clang 14+). Windows uses
WinSock2, IP Helper, SetupAPI and CfgMgr32; no vendor SDK is required. CUDA validation builds only
when an NVIDIA CUDA toolkit is found.

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
```

Options: `HCR_BUILD_TESTS`, `HCR_BUILD_EXAMPLES`, `HCR_BUILD_BENCHMARKS`, `HCR_BUILD_CLI`,
`HCR_BUILD_CUDA_VALIDATION`, `HCR_BUILD_SHARED`, `HCR_ENABLE_ASAN`, `HCR_WARNINGS_AS_ERRORS`
(default ON), `HCR_CUDA_ARCHITECTURES` (default `native`).

First-party code builds warning-clean under `/W4 /WX /permissive- /Zc:__cplusplus` (MSVC) and
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` (GCC/Clang) in Debug, Release and
AddressSanitizer configurations.

## 20. Tests

```bash
ctest --test-dir build/release --output-on-failure
# or run the suites directly
build/release/bin/hcr_tests
build/release/bin/hcr_distributed_tests
build/release/bin/hcr_hardware_tests
```

* **hcr_tests** — identity, values, conditions, catalog, ingestion, generations, staleness,
  contradiction, quirks, compatibility, comparison, snapshots, determinism, resource bounds,
  persistence and protocol adversarial suites, plus seeded randomized property tests and
  deterministic race tests.
* **hcr_distributed_tests** — real OS processes: a publisher is killed with `TerminateProcess`, its
  boot identity is fenced, replays are refused, a replacement publisher receives a fresh boot; the
  coordinator is stopped and restarted against its durable state, the epoch advances, old-epoch
  frames are refused, curated knowledge survives while live evidence requires revalidation.
* **hcr_hardware_tests** — the real adapters on this machine, asserting only what they may claim.

No test applies a timeout of any kind: readiness is observed by blocking on a child's output pipe
and death by waiting on its process handle. A hanging test is a defect by definition.

## 21. Command line

```bash
hcr discover --hardware --save state.bin     # run the real adapters into a registry
hcr coordinator --port 47811 --state state.bin --persist
hcr publisher --port 47811 --publisher publisher.hardware --hardware --linger
hcr devices        --port 47811
hcr caps           --device <id> --port 47811
hcr capability     --device <id> --capability cap.compute.fp8 --explain --port 47811
hcr unknown --device <id>       hcr unsupported --device <id>
hcr conditional --device <id>   hcr stale --device <id>   hcr contradictions --device <id>
hcr quirks --device <id> --applicable        hcr compat --reference hardware-family:family.x
hcr compare --left <id> --right <id>         hcr fleet --capability cap.net.rdma
hcr publishers --port 47811                  hcr epoch --port 47811
hcr classify --device <id>                   hcr snapshot --port 47811
hcr fence --port 47811 --publisher <id>      hcr stop --port 47811
```

Read commands answer from a live coordinator (`--port`) or from a persisted image (`--state`);
administrative mutation is confined to explicit commands. Reading a persisted image shows live
evidence as `REVALIDATION_REQUIRED` — that is the point of the recovery model, not a defect.

## 22. Examples

Eleven runnable examples use only the public API: `ex01_register_and_query`,
`ex02_real_capability_publication`, `ex03_conditional_capability`, `ex04_unsupported_capability`,
`ex05_device_reset_and_stale_evidence`, `ex06_quirk_application`, `ex07_contradictory_evidence`,
`ex08_hardware_comparison`, `ex09_publisher_fencing`, `ex10_persistence_and_recovery`,
`ex11_synthetic_fleet`.

## 23. CMake install and downstream usage

```bash
cmake --install build/release --prefix /some/clean/prefix
```

Installs headers, the library, `HardwareCapabilityRegistryConfig.cmake`, the targets export and a
version file. A downstream project needs only:

```cmake
find_package(HardwareCapabilityRegistry CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE SummonSoftwareLabs::HardwareCapabilityRegistry)
```

`examples/downstream` is an independent project that is built outside the source tree against
installed artifacts only:

```bash
cmake -S examples/downstream -B build/downstream -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/some/clean/prefix
cmake --build build/downstream
build/downstream/hcr_downstream_consumer
```

## 24. Benchmarks

`build/release/bin/hcr_benchmarks` measures completed operations at 1, 100, 1,000 and 10,000 devices
(8 capabilities each) and reports best and median wall time plus per-operation cost. These are
measurements from the machine that produced this release (Release build, MSVC 19.44, Ryzen 7
9800X3D), not estimates:

| operation (median per op) | 1 device | 100 | 1,000 | 10,000 |
| --- | --- | --- | --- | --- |
| register device | 2.4 µs | 2.5 µs | 2.6 µs | 2.5 µs |
| publish capability | 0.7 µs | 0.7 µs | 0.7 µs | 0.7 µs |
| query one capability | 0.81 µs | 1.02 µs | 1.09 µs | 1.09 µs |
| list one device's capabilities (8) | 6.99 µs | 7.88 µs | 8.33 µs | 7.91 µs |
| compare two devices | 13.2 µs | 15.0 µs | 15.6 µs | 21.0 µs |
| quirk resolution | 0.093 µs | 0.157 µs | 0.149 µs | 0.164 µs |
| compatibility lookup | 0.062 µs | 0.062 µs | 0.062 µs | 0.062 µs |
| fleet query (all devices) | 1.4 µs | 153 µs | 1.72 ms | 14.8 ms |
| snapshot | 14.7 µs | 1.95 ms | 19.1 ms | 264 ms |
| persistence save | 698 µs | 3.33 ms | 19.0 ms | 259 ms |
| persistence load | 117 µs | 1.39 ms | 11.4 ms | 109 ms |

Two pathological scalings were found and fixed during hardening, both verified by these numbers: a
per-device capability listing used to scan the entire key space (117 µs at 1 device to 294 µs at
1,000 in the Debug build) and a device comparison did the same. Both now walk a per-device capability
index, so they are proportional to the device rather than to the registry, and the per-device
capability limit check no longer scans every key. Fleet queries, snapshots and persistence are
inherently proportional to the number of subjects and stay linear.

## 25. Resource bounds

Devices, hardware families, capability schemas, capabilities per device, quirks, compatibility
facts, retained evidence history, publishers, metadata and identifier lengths, discovery payloads,
frame size, query results, comparison entries, snapshot records, persistence size and connection
counts are all bounded by `RegistryLimits`. Retention is deterministic: the oldest record that has
already been superseded inside its own source slot is evicted first, and the newest record of every
slot is never evicted. When no record can be evicted without losing current evidence, ingestion is
refused with explicit backpressure rather than silently claiming an incomplete capability state.

## 26. Genuine limitations

* **Platform coverage.** Real discovery adapters are implemented for Windows hosts. Linux sysfs,
  ROCm SMI and vendor DPU/SmartNIC APIs are represented in the source-class and capability model but
  have no adapter in this release, so those capabilities answer UNKNOWN on a Linux host.
* **No CXL device is present on this machine,** so CXL support is exercised only through synthetic
  profiles and honest absence reporting, never against real CXL hardware.
* **MIG, ECC and peer-direct paths cannot be validated on the RTX 5090** used for validation: NVML
  reports them as not supported. They are therefore reported as UNSUPPORTED for that device and are
  exercised only synthetically.
* **No multi-GPU configuration exists here,** so peer-to-peer capability and NVLink-class facts are
  not claimed for real hardware.
* **NVML's memory total and the CUDA driver API's total can differ slightly** between vendor APIs on
  the same device; the registry treats such disagreement as ordinary contradictory evidence and
  resolves it by declared authority instead of averaging it away.
* **The curated datatype table is architecture knowledge, not device proof.** It is published with
  CURATED provenance and precision, and it is deliberately conservative: it does not assert FP64,
  FP8 or FP4 for the consumer Blackwell architecture, so those answer UNKNOWN until something proves
  them.
* **Real NIC discovery cannot prove RDMA, SR-IOV, offload or SmartNIC capability** through the OS
  APIs used, so those answers stay UNKNOWN on this platform.
* **Evidence history is bounded by configuration,** so a long-running deployment retains recent
  evidence per source slot rather than an unbounded audit log.
* **The distributed protocol is not encrypted or authenticated.** It binds loopback, and trust is
  established by publisher registration and coordinator epoch rather than by cryptography.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
