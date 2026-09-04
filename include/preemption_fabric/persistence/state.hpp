#pragma once

#include <cstdint>
#include <vector>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"
#include "preemption_fabric/domains/safepoint.hpp"
#include "preemption_fabric/domains/release.hpp"

namespace pf::persist {

// The durable preemption state that survives a coordinator/runtime restart.
//
// Only state that has genuinely crossed a commit boundary is persisted. Dynamic
// facts (worker liveness, quiescence observations, resource availability) are
// intentionally *not* persisted as fresh: after recovery they must be
// revalidated, and stale WorkerBootIds remain fenced.
//
// The binary format is versioned and integrity checked (see store.hpp).
struct PersistentSafePoint {
  SafePointId id;
  SafePointGeneration generation;
  ExecutionId execution_id;
  AttemptId attempt_id;
  WorkerId worker_id;
  WorkerBootId worker_boot_id;
  std::uint64_t progress_position = 0;
  bool state_capture_required = false;
  bool checkpoint_required = false;
  SafePointState state = SafePointState::kUnknown;
  std::uint64_t order = 0;
};

struct PersistentReleaseReceipt {
  ResourceClass resource = ResourceClass::kOther;
  std::uint64_t units = 0;
  ResourceContractId contract_id;
  ResourceContractGeneration contract_generation;
  ReleaseGeneration release_generation;
  WorkerBootId worker_boot_id;
};

struct PersistentSuperseded {
  PreemptionRequestId request_id;
  PreemptionRequestGeneration generation;
  LifecycleState final_state = LifecycleState::kSuperseded;
};

struct PersistentState {
  std::uint32_t format_version = 0;

  CoordinatorEpoch coordinator_epoch;
  PolicyGeneration policy_generation;

  WorkloadId workload_id;
  WorkloadGeneration workload_generation;
  ExecutionId execution_id;
  ExecutionGeneration execution_generation;
  AttemptId attempt_id;
  AttemptGeneration attempt_generation;

  WorkerId worker_id;
  WorkerBootId worker_boot_id;     // current incarnation (may be stale after restart)
  WorkerBootId fenced_worker_boot; // a boot id fenced by the last preemption

  LifecycleState lifecycle = LifecycleState::kInvalid;

  PreemptionRequestId request_id;
  PreemptionRequestGeneration request_generation;

  bool quiesced = false;
  QuiescenceGeneration quiescence_generation;

  bool state_durable = false;
  StateCaptureGeneration state_capture_generation;
  CheckpointRef checkpoint_ref;
  CheckpointGeneration checkpoint_generation;

  bool resources_released = false;
  ReleaseGeneration release_generation;

  bool preempted = false;
  ResumeGeneration resume_generation;

  std::uint64_t durable_progress = 0;
  ProgressGeneration progress_generation;

  std::vector<PersistentSafePoint> safe_points;
  std::vector<PersistentReleaseReceipt> release_receipts;
  std::vector<PersistentSuperseded> history;
};

}  // namespace pf::persist
