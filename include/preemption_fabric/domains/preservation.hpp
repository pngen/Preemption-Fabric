#pragma once

#include <cstdint>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A state-preservation requirement. Preemption Fabric decides *whether* state
// must be preserved, *which generation* is authoritative, *whether* capture
// completed, and *whether* the result is sufficient for resume. It does not own
// checkpoint contents or storage.
struct PreservationRequirement {
  PreservationGeneration preservation_generation;

  StateCaptureId capture_id;
  StateCaptureGeneration capture_generation;

  // Checkpoint/state identity produced by the owning Checkpoint Fabric.
  CheckpointRef checkpoint_ref;
  CheckpointGeneration checkpoint_generation;

  bool required = false;
  bool completed = false;
  StatePreservationOutcome outcome = StatePreservationOutcome::kUnknown;

  // The generation whose state is authoritative for the current preemption.
  ExecutionGeneration execution_generation;
  AttemptGeneration attempt_generation;
  WorkerBootId worker_boot_id;

  bool equals_required_checkpoint(const CheckpointGeneration& expected) const noexcept {
    return checkpoint_generation == expected;
  }
};

}  // namespace pf
