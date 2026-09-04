#pragma once

#include <vector>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A single quiescence observation for one category. Evidence is generation- and
// provenance-bound so recovered dynamic evidence cannot silently remain current
// after a coordinator restart or after a worker reincarnates.
struct QuiescenceObservation {
  QuiescenceCategory category = QuiescenceCategory::kInvalid;
  bool quiesced = false;
  QuiescenceGeneration generation;
  WorkerBootId worker_boot_id;
  EvidenceKind provenance = EvidenceKind::kUnknown;
  std::uint64_t order = 0;
};

// A complete quiescence report. Quiescence means future mutation from the old
// execution authority has stopped or is fenced and all operations required by
// the preemption contract have reached an acceptable boundary.
struct QuiescenceReport {
  QuiescenceGeneration generation;
  WorkerId worker_id;
  WorkerBootId worker_boot_id;
  bool fully_quiesced = false;
  std::vector<QuiescenceObservation> observations;
  EvidenceKind provenance = EvidenceKind::kUnknown;
};

}  // namespace pf
