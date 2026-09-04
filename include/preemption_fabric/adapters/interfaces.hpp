#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

// Narrow, reference-integration interfaces to the adjacent runtimes.
//
// Preemption Fabric deliberately does NOT own these subsystems:
//   - Execution Fabric owns authoritative execution attempts and logical commit.
//   - Workload Fabric owns durable workload lifecycle.
//   - Checkpoint Fabric owns checkpoint capture/restore state semantics.
//   - Resource Broker owns resource arbitration.
//   - Dependency Fabric owns dependency readiness and invalidation.
//   - Failure Fabric owns generic failure ambiguity and recovery ownership.
//
// Preemption Fabric asks each owning runtime only the narrow questions it needs
// to prove a safe interruption: is execution still authoritative, is a required
// subsystem quiesced, did capture complete, are resources released, are
// dependencies current, and how should a genuine failure be reported.
//
// Every observable is generation- and provenance-bound so that recovered
// dynamic facts (worker liveness, quiescence, resource availability) cannot
// silently remain current after a coordinator restart or worker reincarnation.
namespace pf::adapter {

// ---------------------------------------------------------------------------
// Execution Fabric
// ---------------------------------------------------------------------------
struct AttemptAuthorityView {
  bool authoritative = false;
  ExecutionId execution_id;
  ExecutionGeneration execution_generation;
  AttemptId attempt_id;
  AttemptGeneration attempt_generation;
  CoordinatorEpoch coordinator_epoch;
  WorkerBootId worker_boot_id;
  EvidenceKind provenance = EvidenceKind::kUnknown;
};

class IExecutionFabric {
 public:
  virtual ~IExecutionFabric() = default;

  // The currently-authoritative attempt view for an execution scope.
  [[nodiscard]] virtual AttemptAuthorityView current_attempt_authority(ExecutionId execution,
                                                                       AttemptId attempt) const = 0;

  // Fence an attempt so it can no longer publish progress or completion.
  virtual bool fence_attempt(ExecutionId execution, AttemptId attempt, ExecutionGeneration generation) = 0;

  // Grant a fresh attempt authority for a subsequent resume.
  [[nodiscard]] virtual AttemptAuthorityView grant_fresh_attempt_authority(
      ExecutionId execution, AttemptId attempt, ExecutionGeneration generation, WorkerBootId worker) = 0;
};

// ---------------------------------------------------------------------------
// Workload Fabric
// ---------------------------------------------------------------------------
enum class WorkloadLifecycleState : std::uint32_t {
  kActive = 0,
  kSuspended = 1,
  kPreempted = 2,
  kCancelled = 3,
  kCompleted = 4,
  kUnknown = 5
};

class IWorkloadFabric {
 public:
  virtual ~IWorkloadFabric() = default;
  [[nodiscard]] virtual WorkloadLifecycleState workload_state(WorkloadId workload) const = 0;
  virtual void notify_preempted(WorkloadId workload, WorkloadGeneration generation, ExecutionId execution,
                                ExecutionGeneration execution_generation) = 0;
  virtual void notify_resumed(WorkloadId workload, WorkloadGeneration generation, ExecutionId execution,
                              ExecutionGeneration execution_generation, ResumeGeneration resume_gen) = 0;
};

// ---------------------------------------------------------------------------
// Checkpoint Fabric / Checkpoint Store
// ---------------------------------------------------------------------------
struct StateCaptureRequest {
  bool required = false;
  CheckpointGeneration expected_generation;
  StateCaptureGeneration capture_generation;
  ExecutionGeneration execution_generation;
  AttemptGeneration attempt_generation;
};

struct CaptureResult {
  bool completed = false;
  bool success = false;
  StateCaptureId capture_id;
  StateCaptureGeneration capture_generation;
  CheckpointRef checkpoint_ref;
  CheckpointGeneration checkpoint_generation;
  EvidenceKind provenance = EvidenceKind::kSynthetic;
};

class ICheckpointFabric {
 public:
  virtual ~ICheckpointFabric() = default;

  // Request a capture of the given generation's state.
  [[nodiscard]] virtual CaptureResult request_capture(const StateCaptureRequest& request) = 0;
  [[nodiscard]] virtual CaptureResult query_capture(StateCaptureId capture) = 0;

  // Verify that a checkpoint reference identifies the required generation.
  [[nodiscard]] virtual bool validate_checkpoint(CheckpointRef ref, CheckpointGeneration expected) const = 0;

  // Restore state for resume. Returns true only if the generation matches.
  virtual bool restore(CheckpointRef ref, CheckpointGeneration expected) = 0;
};

// ---------------------------------------------------------------------------
// Resource Broker
// ---------------------------------------------------------------------------
struct ResourceLease {
  ResourceClass resource = ResourceClass::kOther;
  std::uint64_t units = 0;
};

struct ResourceContract {
  ResourceContractId id;
  ResourceContractGeneration generation;
  std::vector<ResourceLease> leases;
};

struct ReleaseResult {
  bool success = false;
  ResourceContractGeneration release_generation;
  WorkerBootId worker_boot_id;
};

struct ReacquireResult {
  bool success = false;
  ResourceContractGeneration contract_generation;
  WorkerBootId worker_boot_id;
};

class IResourceBroker {
 public:
  virtual ~IResourceBroker() = default;

  [[nodiscard]] virtual ResourceContract current_contract(ExecutionId execution) const = 0;

  [[nodiscard]] virtual ReleaseResult request_release(ExecutionId execution, ResourceClass resource,
                                                      std::uint64_t units,
                                                      ResourceContractGeneration expected_contract) = 0;

  [[nodiscard]] virtual ReacquireResult reacquire(ExecutionId execution, ResourceClass resource,
                                                  std::uint64_t units,
                                                  ResourceContractGeneration contract) = 0;
};

// ---------------------------------------------------------------------------
// Dependency Fabric
// ---------------------------------------------------------------------------
enum class DependencyStatus : std::uint32_t {
  kCurrent = 0,
  kStale = 1,
  kMissing = 2,
  kUnknown = 3
};

struct DependencyView {
  DependencyStatus status = DependencyStatus::kUnknown;
  DependencyGeneration generation;
  bool revalidation_required = false;
  bool recompute_required = false;
};

class IDependencyFabric {
 public:
  virtual ~IDependencyFabric() = default;
  [[nodiscard]] virtual DependencyView dependency_status(DependencyId dependency) = 0;
  [[nodiscard]] virtual DependencyView resume_dependencies(ExecutionId execution) = 0;
};

// ---------------------------------------------------------------------------
// Failure Fabric
// ---------------------------------------------------------------------------
enum class FailureCategory : std::uint32_t {
  kForcedTermination = 0,
  kAmbiguous = 1,
  kFailed = 2,
  kUnknown = 3
};

class IFailureFabric {
 public:
  virtual ~IFailureFabric() = default;
  virtual void report(FailureCategory category, std::string_view detail) = 0;
};

// ---------------------------------------------------------------------------
// Quiescence provider (narrow: ask an owning runtime whether it is quiesced)
// ---------------------------------------------------------------------------
class IQuiescenceProvider {
 public:
  virtual ~IQuiescenceProvider() = default;
  [[nodiscard]] virtual bool is_quiesced(QuiescenceCategory category) const = 0;
  [[nodiscard]] virtual std::vector<QuiescenceCategory> categories() const = 0;
};

}  // namespace pf::adapter
