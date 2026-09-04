#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A safe point is a first-class runtime object describing an execution boundary
// at which interruption is known to preserve a specified invariant. A safe point
// is never inferred merely because a kernel returned; it must be declared (or
// discovered from durable evidence) and validated before it authorizes the
// transition to quiescence.
struct SafePoint {
  SafePointId id;
  SafePointGeneration generation;

  ExecutionId execution_id;
  AttemptId attempt_id;
  WorkerId worker_id;
  WorkerBootId worker_boot_id;

  // Progress position captured at this boundary.
  std::uint64_t progress_position = 0;

  bool state_capture_required = false;
  bool checkpoint_required = false;
  bool resume_requirement = false;

  // Resources that are eligible for release once this boundary is quiesced.
  std::vector<ResourceClass> resource_release_eligibility;

  // A human-oriented description of the invariant this boundary preserves.
  std::string invariant;

  EvidenceKind provenance = EvidenceKind::kUnknown;
  std::uint64_t order = 0;  // monotonic non-wall-clock order tag

  SafePointState state = SafePointState::kUnknown;
};

}  // namespace pf
