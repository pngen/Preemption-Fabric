# Preemption Fabric

**Preemption Fabric is an open-source, vendor-neutral C++20 runtime for governing safe preemption, quiescence, checkpointability, state preservation, resource release, partial progress, priority inversion, and resume eligibility across heterogeneous accelerator infrastructure.**

It answers one systems question:

**Can this work be interrupted safely now, what state and progress must survive, which resources may be reclaimed, and under what authority and conditions may execution resume?**

Preemption Fabric exists because stopping accelerator work is not equivalent to killing a process. A scheduler or resource arbiter may decide that capacity should be reclaimed; Execution Fabric may revoke or replace execution authority; Workload Fabric may move a workload into a preempted or suspended lifecycle state; Checkpoint Fabric may capture and restore execution state; Resource Broker may arbitrate the reclaimed resources; Failure Fabric may govern ambiguous or failed termination; Dependency Fabric may determine which dependencies become stale or require revalidation. Preemption Fabric owns the *safe interruption protocol itself*: whether work is currently preemptible, where the next valid preemption boundary exists, how work reaches quiescence, what progress is durable versus speculative, what state must be preserved before resources are released, which release obligations must complete, whether interruption completed safely, what was lost or must be recomputed, whether resume is currently eligible, what must be revalidated before resume, and what evidence proves the preemption and resume transitions were legitimate.

## Core principle

A preemption request is not a completed preemption. A process exit is not a safe preemption. A checkpoint request is not a durable checkpoint. A released allocation is not proof that execution state survived. A resume request is not proof that resume is valid.

Preemption is complete only when the runtime can prove that: **execution stopped at a valid boundary → required state became durable or was proven recomputable → future stale execution was fenced → required resources were released → resume obligations were recorded → later execution resumed only under fresh valid authority.**

## Adjacent boundaries

Preemption Fabric is deliberately narrow. It does **not** own:

- Execution Fabric — governs authoritative execution attempts and logical commit.
- Workload Fabric — governs durable workload lifecycle.
- Checkpoint Fabric — governs checkpoint capture/restore state semantics.
- Checkpoint Store — governs checkpoint bytes/storage.
- Resource Broker — governs resource arbitration.
- Dependency Fabric — governs dependency readiness and invalidation.
- Failure Fabric — governs generic failure ambiguity and recovery ownership.
- A general scheduler, resource allocator, workload lifecycle runtime, checkpoint storage engine, execution-attempt authority system, generic failure-classification runtime, dependency graph, or process supervisor.

Those boundaries are explicit: Preemption Fabric implements narrow interfaces (see `include/preemption_fabric/adapters/interfaces.hpp`) and always delegates to the owning runtime. The reference adapters (`include/preemption_fabric/adapters/reference.hpp`) are deterministic, in-process stand-ins for those sibling runtimes, so standalone tests are possible without source-level coupling to any sibling repository.

## Defining thesis

> Preemption is not process termination. It is a governed transition from live execution to a proven resumable boundary where authoritative progress is preserved, stale execution is fenced, required resources are released, and future execution may resume only under fresh valid authority.

## Strongly typed identity and authority model

Preemption Fabric refuses to collapse semantically different authority domains into a generic integer. Every identity domain has a distinct C++ type (see `include/preemption_fabric/core/identity.hpp`), including: `PreemptionRequestId`/`Generation`, `PreemptionPlan`, `PreemptionAttempt`, `Workload`, `Execution`, `Attempt`, `WorkerId`/`WorkerBootId`, `CoordinatorEpoch`, `SafePoint`, `Quiescence`, `Progress`, `StateCapture`, `CheckpointRef`/`Generation`, `Preservation`, `ResourceContract`, `Release`, `Resume`, `Dependency`, `Priority`, `Policy`, and `Authority` generations.

An old `PreemptionRequestGeneration` cannot authorize a new execution attempt. An old `WorkerBootId` cannot publish quiescence or release evidence after worker reincarnation. An old checkpoint generation cannot silently satisfy a current resume requirement. A stale resource-release acknowledgment cannot complete a current preemption. These are enforced by type, then at runtime by generation fencing in the domain logic.

## Preemption lifecycle

An explicit, guarded state machine (`include/preemption_fabric/lifecycle/lifecycle.hpp`) implements the lifecycle: `REQUESTED → VALIDATING_AUTHORITY → ASSESSING → DEFERRED → WAITING_FOR_SAFE_POINT → SAFE_POINT_REACHED → QUIESCING → QUIESCED → CAPTURE_REQUIRED → CAPTURING_STATE → STATE_DURABLE → RELEASE_PENDING → RELEASING → RESOURCES_RELEASED → PREEMPTED → RESUME_PENDING → REVALIDATING → RESTORING → RESUME_READY → RESUMING → RESUMED`, with terminal `CANCELLED / ABORTED / FAILED / SUPERSEDED`. Transitions are explicit and deterministic; illegal transitions are rejected before any side effect. In particular: `REQUESTED → RESOURCES_RELEASED` without quiescence is impossible, `PREEMPTED → RESUMED` without fresh authority is impossible, a stale worker cannot publish quiescence, a stale checkpoint cannot produce `RESUME_READY`, and a superseded preemption cannot become current.

## Safe points and preemptibility

Safe points are first-class runtime objects representing an execution boundary at which interruption preserves a specified invariant. Safe points may be `DECLARED / REACHED / VALIDATING / VALID / INVALID / SUPERSEDED / STALE / UNKNOWN`. A safe point is never inferred merely because a kernel returned; it must be declared and validated before it authorizes quiescence.

Preemptibility is assessed explicitly (`PREEMPTIBLE_NOW / PREEMPTIBLE_AT_SAFE_POINT / PREEMPTIBLE_AFTER_STATE_CAPTURE / PREEMPTIBLE_AFTER_COMMIT_BOUNDARY / RECOMPUTABLE_FROM_DURABLE_PROGRESS / TEMPORARILY_UNSAFE / NON_PREEMPTIBLE / UNSUPPORTED / UNKNOWN`). `UNKNOWN` never silently becomes `PREEMPTIBLE`. The runtime exposes typed reasons for why work is or is not preemptible (unsafe phase, mutation not at a stable boundary, progress not durable, outstanding async not quiesced, transfer in flight, state capture required, checkpoint backend unavailable, resource release not yet possible, stale execution authority, policy prohibition, explicitly non-preemptible).

## Quiescence, preservation, release, resume

Quiescence is explicit and category-based (`CPU_WORKER_ACTIVITY / ACCELERATOR_KERNELS / ASYNCHRONOUS_COPIES / QUEUED_DEVICE_OPERATIONS / HOST_CALLBACKS / STORAGE_WRITES / NETWORK_ACTIVITY / STATE_PUBLICATION / COMPLETION_PUBLICATION / PENDING_RESOURCE_MUTATION`). The runtime does not own these subsystems; it asks narrow interfaces whether each is quiesced, and requires generation- and provenance-bound evidence from the current worker incarnation.

State preservation decides whether capture is required, which generation is authoritative, whether capture completed, and whether the result is sufficient for resume. Outcomes distinguish `ALREADY_DURABLE / CAPTURE_REQUIRED / CAPTURE_IN_PROGRESS / CAPTURED_AND_VERIFIED / RECOMPUTABLE / NO_DURABLE_STATE / CAPTURE_FAILED / STALE_CAPTURE / UNKNOWN`. A capture failure never produces `STATE_DURABLE`.

Resource release is a first-class phase. Preemption Fabric does not arbitrate who receives the resource next; it governs whether the current execution has satisfied its release obligations. Release receipts are generation-bound; a stale receipt never alters current accounting. Where the runtime controls accounting, it closes exactly (verified in tests).

Resume is a first-class authority decision. A checkpoint alone never makes a workload resume-eligible. The runtime evaluates fresh execution authority, current `WorkerBootId`, current coordinator epoch, the correct preempted generation, correct checkpoint generation, current dependencies, a valid resource contract, device/runtime capability, state integrity, locality/migration compatibility, policy, no superseding cancellation/termination, and prior resource release. Outcomes are structured (`RESUME_ELIGIBLE / RESUME_BLOCKED_STALE_AUTHORITY / RESUME_BLOCKED_STATE_MISSING / RESUME_BLOCKED_STATE_STALE / RESUME_BLOCKED_DEPENDENCY / RESUME_BLOCKED_RESOURCE / RESUME_BLOCKED_COMPATIBILITY / RESUME_BLOCKED_POLICY / RESUME_REVALIDATION_REQUIRED / RESUME_RESTORE_REQUIRED / RESUME_RECOMPUTE_REQUIRED / RESUME_NOT_ALLOWED / UNKNOWN`). `UNKNOWN` never becomes `RESUME_ELIGIBLE`.

## Partial progress and cost evidence

Partial progress is modeled explicitly (`ATTEMPTED / SPECULATIVE / ACKNOWLEDGED / COMMITTED_LOGICAL / DURABLE / RECOMPUTABLE / DISCARDED / LOST`). A preemption plan identifies the last durable progress boundary, the current execution position, the maximum rollback/recompute requirement, state required to preserve additional progress, and an honest estimate of lost work. Progress never moves backward within the same authoritative generation; a rollback is a fresh transition. Old progress publications are generation-fenced; a stale attempt cannot advance the durable progress record.

Cost/timing evidence is separated into `MEASURED / DERIVED / ESTIMATED / SYNTHETIC / UNKNOWN`; estimated preemption cost is never presented as measured, and wall-clock time is never part of logical identity.

## Cooperative preemption vs forced termination

This distinction is mandatory. The runtime reports `SAFE_PREEMPTION_COMPLETED / DEFER_TO_SAFE_POINT / STATE_CAPTURE_REQUIRED / CANNOT_PREEMPT_SAFELY / NON_PREEMPTIBLE / FORCE_TERMINATION_REQUIRED / PREEMPTION_ABORTED / PREEMPTION_FAILED`. A forced OS process termination is never reported as a successful safe preemption: the runtime fences the old execution, routes failure/ambiguity semantics through the owning Failure Fabric interface, never claims preserved progress without durable evidence, and never claims `PREEMPTED` merely because a process died.

## Explainability

Callers can ask whether work is preemptible now, why not, what safe point is required, how much progress is durable, what would be lost, what must be captured, what resources must be released, which release obligations remain, why execution is not yet `PREEMPTED`, whether interruption was safe or forced, what stale authority attempted mutation, whether resume is eligible, why resume is blocked, what must be revalidated/restored, whether the work can resume on another compatible device, and which policy/generation drove a decision. Explanations are deterministic and derived from explicit state, using typed reasons plus human-readable text.

## Priority-inversion evidence

The runtime exposes a dedicated, typed priority-inversion evidence surface (`include/preemption_fabric/priority/inversion.hpp`) without becoming a scheduler. `priority_inversion_assessment(...)` takes caller-supplied facts — the low-priority holder, the higher-priority blocked work, the exact resource causing the inversion, the current priority generation, whether the holder is authoritative and its preemptibility, time/work to the next safe point, preservation/release/resume-recompute cost, measured blocking duration, and the reason the inversion exists — and returns deterministic evidence: the exact blocking resource, the priority relationship, per-phase costs, whether safe preemption would resolve it, and a `MEASURED / DERIVED / ESTIMATED / SYNTHETIC / UNKNOWN` provenance. A stale priority generation is rejected; UNKNOWN evidence never becomes a positive recommendation (`positive_recommendation()` is false). A dedicated test (`tests/test_priority_inversion.cpp`) proves holder-blocks-higher-priority identification, stale-generation rejection, and UNKNOWN-never-positive. This is policy input only; the caller decides whether to request preemption.

## Migration compatibility

Preemption Fabric does not place work. It exposes `evaluate_migration_destination(const DeviceCapability&)`, which evaluates an externally supplied destination against the preserved state and current generations and returns `MigrationCompatibility`: an incompatible destination is rejected before resume; a stale destination/capability generation is rejected; current dependency/resource/policy generations are required; and resume proceeds only under fresh execution/resume authority. The owning scheduler supplies the destination; the runtime only reports deterministic compatibility evidence through the narrow `ICompatibilityEvaluator` interface. A dedicated integration test (`tests/test_migration.cpp`) proves safe preemption, preserved-state generation binding, rejection of an intentionally incompatible destination and of a stale capability generation, acceptance of a compatible destination, dependency invalidation blocking migration until revalidated, and resume under fresh authority. No placement logic is implemented; no real second-GPU migration is claimed (destination capability used in tests is SYNTHETIC).

## Persistence and recovery

Durable preemption state is persisted with a versioned binary format and deterministic CRC-32 integrity checking (`include/preemption_fabric/persistence/`). The decoder rejects bad magic, bad version, truncation, checksum failure, invalid enums, malformed lengths, trailing garbage, duplicate IDs, impossible generation regression, multiple current authoritative generations, illegal lifecycle states, release-before-quiescence, resume-before-preempted, broken checkpoint references, invalid resource obligations, malformed progress, and integer overflow. Dynamic facts (worker liveness, quiescence observations, resource availability) are never persisted as fresh: after restart, quiescence tied to a dead process is fenced, resource availability is revalidated, device capability/locality is revalidated, resume eligibility is recomputed under current authority, stale `WorkerBootId`s remain fenced, and dynamic resume prerequisites become `REVALIDATION_REQUIRED` as appropriate.

## Protocol and real multiprocess proof

A compact, versioned, framed TCP protocol (`include/preemption_fabric/protocol/`) carries `HELLO / REGISTER / BEGIN_EXECUTION / PUBLISH_PROGRESS / DECLARE_SAFE_POINT / REACH_SAFE_POINT / REQUEST_PREEMPTION / PREEMPTION_ASSESSMENT / BEGIN_QUIESCE / QUIESCED / REQUEST_STATE_CAPTURE / STATE_CAPTURED / REQUEST_RELEASE / RESOURCE_RELEASED / PREEMPTED / REQUEST_RESUME / REVALIDATION_RESULT / RESTORE_RESULT / RESUMED / CANCEL / COMPLETE / ERROR / SHUTDOWN`. Frames use explicit magic/version, bounded payload sizes, CRC-32 integrity, checked arithmetic, complete partial-read/write handling, malformed-length rejection, invalid-enum rejection, and trailing-data rejection. Only typed identities and generations appear on the wire — never raw pointers or process-local addresses.

The reference coordinator (`pf_coordinator`) and worker (`pf_worker`) are real, independent OS processes communicating over real loopback TCP. A real multiprocess test (`tests/test_multiprocess.cpp`) proves: registration with a fresh `WorkerBootId`; segmented execution; a valid preemption request; `PREEMPTIBLE_AT_SAFE_POINT` deferral; reaching the next safe point; quiescence; state capture; resource release with exact accounting; `PREEMPTED`; rejection of replayed stale requests and conflicting duplicates; a fresh `ResumeGeneration`; revalidation of dependencies/resources/state; resume under fresh execution authority; continuation of work from the preserved boundary; an old attempt that cannot publish authoritative progress after preemption; a worker killed before the durable capture boundary (reported as forced, not safe preemption); and coordinator restart with durable-state recovery.

## Property, concurrency, and adversarial testing

- **Property tests** (`tests/test_property.cpp`): 200 deterministic seeded trials over randomized preemption event sequences (request/execution generations, worker boots, safe-point spacing, progress boundaries, capture success/failure, release ordering, cancellation, completion, resumption, dependency/resource/policy changes), proving monotonic durable progress, no release-before-quiescence in durable states, round-trippable persistence, and a single authoritative preemption generation.
- **Concurrency tests** (`tests/test_concurrency.cpp`): 16 threads × 2000 iterations of mutations and read-heavy queries on one runtime, proving no data races and internal consistency (no arbitrary sleeps are used as the proof).
- **Adversarial tests** (`tests/test_adversarial.cpp`): stale epoch/attempt-generation/boot requests, conflicting duplicates, forged safe points, backward-progress advances, release-before-quiescence, `PREEMPTED`-before-release, resume-before-preempted, superseded checkpoint generations, and cancel-vs-stale-resume precedence.
- **Priority-inversion test** (`tests/test_priority_inversion.cpp`): low-priority holder blocks higher-priority work; the evidence identifies the exact blocking resource and priority relationship; stale priority generation is rejected; UNKNOWN evidence never becomes a positive recommendation.
- **Migration compatibility test** (`tests/test_migration.cpp`): safe preemption; preserved state bound to the current checkpoint/state generation; original placement not assumed; externally supplied destination evaluated; intentionally incompatible destination rejected before resume; compatible destination accepted; current dependency/resource/policy generations required; stale destination/capability generation rejected; resume under fresh execution/resume authority.
- **Kill-after-preservation worker-death test** (`tests/test_kill_after_preservation.cpp`): a real OS worker reaches a durable `PREEMPTED` state, is killed, and is replaced by a fresh worker with a new `WorkerBootId` on the SAME coordinator; stale preemption/release messages are replayed and rejected; the durable `PREEMPTED` state remains authoritative; dynamic worker/resource evidence is revalidated; fresh resume authority is issued; the replacement worker resumes from the preserved boundary and continues.

No test timeouts are used anywhere. A hanging test is treated as a lifecycle defect to diagnose, never as a pass.

## CUDA physical proof (NVIDIA RTX 5090, sm_120)

A real CUDA-backed proof (`cuda/cuda_proof.cu`) exercises safe cooperative interruption around real accelerator work on an NVIDIA GeForce RTX 5090 (sm_120) with CUDA 12.9. It uses segmented device work with explicit runtime safe points between real kernel segments (real `cudaMalloc`, H2D, kernels, synchronization, D2H, CPU-reference verification, `cudaFree`), and proves:

- **Scenario A**: normal safe preemption; durable/recomputable state; PREEMPTED after preservation + release; reacquire/restore under fresh authority; continue remaining segments; final GPU result matches uninterrupted CPU reference; device memory returns to the measured baseline.
- **Scenario B**: state capture required via real D2H copyback; verified preserved bytes; free; restore into a fresh allocation; resume; CPU parity.
- **Scenario C**: recomputable progress; preempt without preserving all volatile bytes; record the durable/recompute boundary; resume by recomputing only the permitted interval; parity.
- **Scenario D**: request while between safe boundaries returns `DEFER_TO_SAFE_POINT`; no immediate preemption; reach a valid boundary; complete safe preemption.
- **Scenario E**: stale authority is rejected before a kernel launch; fresh authority succeeds.
- **Scenario F/G**: worker loss before the preservation boundary (reported as forced, not safe preemption) and coordinator restart with durable recovery and revalidation (host-simulated, labeled `SYNTHETIC`).

All evidence is labeled. The proof does **not** claim instruction-level or hardware kernel preemption, context preemption, MIG preemption, driver-level kernel preemption, a second GPU, multi-GPU preemption, NVLink, RDMA, GPUDirect, or device-reset behavior not actually exercised. Interruption is cooperative at real kernel boundaries.

## Build, install, and use

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
cmake --install build --config Release --prefix <prefix>
```

The public API remains fully usable without the built-in TCP coordinator; transport is a reference deployment mechanism, not the core abstraction. Install the package and consume it with:

```cmake
find_package(PreemptionFabric CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE PreemptionFabric::PreemptionFabric)
```

An independent downstream consumer lives in `downstream_consumer/` and validates the installed package. The inspection CLI (`pf_inspect`) dumps active preemption state, the authoritative request generation, the current safe point, durable progress, preservation/release obligations, resume blockers, and explanations. Runnable examples are under `examples/`. Two benchmarks are provided: `pf_benchmark` (completed micro-operations) and `pf_benchmark_phases` (an orchestrated full-preemption phase benchmark measuring completed phases — request→assessment, wait to safe point, quiescence, state preservation, resource release, total preemption latency, resume revalidation, restore, resume, and the total preempt→resume cycle — with workload size, safe-point spacing, preservation size, repetitions, and per-phase provenance).

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
