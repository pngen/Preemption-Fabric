#pragma once

#include <cstdint>
#include <string>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// ---------------------------------------------------------------------------
// Priority-inversion evidence.
//
// Preemption Fabric does NOT schedule. It exposes a deterministic, typed
// evidence assessment describing a priority inversion so a caller can decide
// whether to request preemption. This is policy input, never an authority
// decision, and it never mutates the preemption lifecycle.
// ---------------------------------------------------------------------------

// Whether a safe preemption of the holder would resolve the inversion.
enum class PriorityInversionResolution : std::uint32_t {
  kInvalid = 0,
  kSafePreemptionResolves = 1,
  kSafePreemptionDoesNotResolve = 2,
  kUnknown = 3
};

// Caller-supplied facts about an observed (or suspected) priority inversion.
// The runtime validates generations and preemptibility and returns evidence.
struct PriorityInversionInput {
  // The low-priority execution holding the contended resource.
  WorkloadId holder_workload;
  ExecutionId holder_execution;
  AttemptId holder_attempt;
  WorkerBootId holder_worker_boot;

  // The higher-priority work blocked on that resource.
  WorkloadId blocked_workload;
  ExecutionId blocked_execution;

  // The resource whose content is the inversion.
  ResourceClass resource = ResourceClass::kInvalid;

  // Priority relationship generation. The runtime rejects a stale generation.
  PriorityGeneration priority_generation;
  // Authority/policy generation under which this observation is made.
  PolicyGeneration policy_generation;

  bool holder_authoritative = false;
  PreemptibilityClass holder_preemptibility = PreemptibilityClass::kUnknown;

  // Costs / evidence (work to next safe point, preservation, release, resume).
  std::uint64_t next_safe_point_cost = 0;
  std::uint64_t preservation_cost = 0;
  std::uint64_t release_cost = 0;
  std::uint64_t resume_recompute_cost = 0;

  // Blocking duration, only if actually measured.
  std::uint64_t blocking_duration_ns = 0;
  bool blocking_duration_measured = false;

  // Why the inversion exists (human-readable, bounded).
  std::string reason;

  // Provenance of the overall evidence.
  EvidenceKind provenance = EvidenceKind::kEstimated;
};

// The deterministic evidence assessment result.
struct PriorityInversionEvidence {
  bool ok = false;       // false if invalid (e.g. stale generation)
  bool stale = false;    // true if the priority generation was stale

  ResourceClass resource = ResourceClass::kInvalid;
  WorkloadId holder_workload;
  WorkloadId blocked_workload;

  bool holder_preemptible = false;
  std::uint64_t next_safe_point_cost = 0;
  std::uint64_t preservation_cost = 0;
  std::uint64_t release_cost = 0;
  std::uint64_t resume_recompute_cost = 0;
  std::uint64_t blocking_duration_ns = 0;
  bool blocking_duration_measured = false;

  PriorityInversionResolution resolution = PriorityInversionResolution::kUnknown;
  std::string reason;
  EvidenceKind provenance = EvidenceKind::kUnknown;

  // Only true when the evidence is concrete AND safe preemption would resolve it.
  [[nodiscard]] bool positive_recommendation() const noexcept {
    return ok && !stale && resolution == PriorityInversionResolution::kSafePreemptionResolves &&
           provenance != EvidenceKind::kUnknown;
  }
};

}  // namespace pf
