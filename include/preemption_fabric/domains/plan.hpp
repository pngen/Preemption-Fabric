#pragma once

#include <cstdint>
#include <vector>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// How progress is preserved at a boundary: either it must be captured to
// durable storage, or it is legally recomputable from already-durable progress.
enum class PreservationMode : std::uint32_t {
  kInvalid = 0,
  kCapture = 1,
  kRecompute = 2,
  kAlreadyDurable = 3,
  kUnknown = 4
};

// A preemption plan is the runtime's structured answer to "what would a safe
// preemption require here": the last durable progress boundary, the current
// execution position, the maximum rollback/recompute requirement, what must be
// preserved, and an honest estimate of lost work if interrupted now.
struct PreemptionPlan {
  PreemptionPlanId id;
  PreemptionPlanGeneration generation;

  ExecutionId execution_id;
  AttemptId attempt_id;
  WorkloadId workload_id;
  WorkloadGeneration workload_generation;

  PreemptibilityClass preemptibility = PreemptibilityClass::kUnknown;
  std::uint64_t last_durable_progress = 0;
  std::uint64_t current_position = 0;
  std::uint64_t max_rollback = 0;

  PreservationMode preservation_mode = PreservationMode::kUnknown;
  std::vector<ResourceClass> resources_to_release;
  bool release_before_preempted = true;

  std::uint64_t estimated_lost_work = 0;
  std::uint64_t estimated_recompute_work = 0;
  std::uint64_t state_to_preserve_bytes = 0;

  SafePointId next_safe_point;
  std::uint64_t cost_to_next_safe_point = 0;
  bool checkpoint_backend_available = true;

  PreemptionReason suggested_reason = PreemptionReason::kUnknown;
  EvidenceKind provenance = EvidenceKind::kUnknown;
};

}  // namespace pf
