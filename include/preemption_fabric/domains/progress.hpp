#pragma once

#include <cstdint>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A single progress record. Progress never moves backward within the same
// authoritative generation; a rollback is represented as a new transition with
// a fresh generation. Old progress publications are generation-fenced.
struct ProgressRecord {
  ProgressGeneration generation;
  std::uint64_t position = 0;
  ProgressKind kind = ProgressKind::kAttempted;
  WorkerId worker_id;
  WorkerBootId worker_boot_id;
  ExecutionId execution_id;
  AttemptId attempt_id;
  EvidenceKind provenance = EvidenceKind::kUnknown;
  std::uint64_t order = 0;
};

}  // namespace pf
