#pragma once

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

enum class RequirementStatus : std::uint32_t {
  kInvalid = 0,
  kCurrent = 1,
  kStale = 2,
  kMissing = 3,
  kUnknown = 4
};

// A resume obligation records exactly what must be revalidated before a fresh
// ResumeGeneration may authorize continuation of a preempted execution.
struct ResumeObligation {
  ResumeGeneration resume_generation;
  ExecutionId execution_id;
  AttemptId attempt_id;
  WorkloadId workload_id;
  WorkloadGeneration workload_generation;
  ExecutionGeneration preempted_execution_generation;

  CheckpointRef checkpoint_ref;
  CheckpointGeneration checkpoint_generation;

  RequirementStatus dependency_status = RequirementStatus::kUnknown;
  RequirementStatus resource_status = RequirementStatus::kUnknown;
  RequirementStatus capability_status = RequirementStatus::kUnknown;

  bool requires_restore = false;
  bool requires_recompute = false;
  bool revalidation_required = true;
};

}  // namespace pf
