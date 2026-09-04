#pragma once

#include <cstdint>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A preemption request is a typed, generation-bound authoritative claim that a
// specific execution attempt should be safely interrupted. Reason describes the
// motivation but is never used as authority; every request also binds explicit
// workload/execution identity, current execution generation, request
// generation, coordinator epoch, optional worker incarnation, the requesting
// authority generation, and the policy generation in force when it was made.
struct PreemptionRequest {
  PreemptionRequestId id;
  PreemptionRequestGeneration generation;

  WorkloadId workload_id;
  WorkloadGeneration workload_generation;

  ExecutionId execution_id;
  ExecutionGeneration execution_generation;

  AttemptId attempt_id;
  AttemptGeneration attempt_generation;

  CoordinatorEpoch coordinator_epoch;
  WorkerId worker_id = {};
  WorkerBootId worker_boot_id = {};

  PreemptionReason reason = PreemptionReason::kUnknown;
  PolicyGeneration policy_generation;
  AuthorityGeneration authority_generation;

  // The lifecycle stage this request requires before it is considered
  // satisfied. Normally kPreempted; a preemption plan may target a lesser
  // durable stage for a quiet drain.
  LifecycleState required_completion = LifecycleState::kPreempted;

  bool has_deadline = false;
  std::uint64_t deadline_ns = 0;  // monotonic deadline; never wall-clock identity

  // Set when this request is an exact duplicate of an accepted request.
  PreemptionRequestId duplicate_of = {};
};

}  // namespace pf
