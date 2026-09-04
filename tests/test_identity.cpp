#include "tests/test_framework.hpp"
#include "preemption_fabric/core/identity.hpp"
#include "preemption_fabric/core/enums.hpp"

using namespace pf;

int main() {
  TEST_CONTEXT("identity");

  // A PreemptionRequestGeneration and a WorkloadGeneration are distinct types
  // and cannot silently be compared or assigned.
  static_assert(!std::is_same_v<PreemptionRequestGeneration, WorkloadGeneration>, "must be distinct types");
  static_assert(!std::is_same_v<ExecutionGeneration, AttemptGeneration>, "must be distinct types");
  static_assert(!std::is_same_v<PreemptionRequestId, PreemptionRequestGeneration>, "id vs generation");

  PreemptionRequestGeneration g1(1);
  PreemptionRequestGeneration g2(2);
  CHECK(g1 != g2);
  CHECK(g1 < g2);
  CHECK(g2.next() == PreemptionRequestGeneration(3));
  CHECK(PreemptionRequestGeneration(0).is_valid() == false);
  CHECK(PreemptionRequestGeneration(7).is_valid());

  // Generations never move backward: increment only moves forward.
  CHECK(g2 > g1);

  // Enum codec round-trip and UNKNOWN semantics.
  CHECK(to_string(PreemptionReason::kFairness) == "FAIRNESS");
  CHECK(to_string(PreemptionReason::kUnknown) == "UNKNOWN");
  CHECK(preemption_reason_from_string("HIGHER_PRIORITY_WORK").value() == PreemptionReason::kHigherPriorityWork);
  CHECK(preemption_reason_from_string("NOT_A_REASON").has_value() == false);
  CHECK(to_string(PreemptibilityClass::kUnknown) == "UNKNOWN");
  CHECK(lifecycle_state_from_string("PREEMPTED").value() == LifecycleState::kPreempted);

  PF_TEST_RETURN();
}
