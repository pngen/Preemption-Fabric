#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

static PriorityInversionInput make_input(WorkloadId holder, WorkloadId blocked, ResourceClass resource,
                                         PriorityGeneration gen, PreemptibilityClass preemptible,
                                         bool authoritative, PolicyGeneration policy,
                                         EvidenceKind provenance) {
  PriorityInversionInput in;
  in.holder_workload = holder;
  in.holder_execution = ExecutionId(20);
  in.holder_attempt = AttemptId(30);
  in.holder_worker_boot = WorkerBootId(1);
  in.blocked_workload = blocked;
  in.blocked_execution = ExecutionId(21);
  in.resource = resource;
  in.priority_generation = gen;
  in.policy_generation = policy;
  in.holder_authoritative = authoritative;
  in.holder_preemptibility = preemptible;
  in.next_safe_point_cost = 5;
  in.preservation_cost = 10;
  in.release_cost = 3;
  in.resume_recompute_cost = 8;
  in.blocking_duration_ns = 42;
  in.blocking_duration_measured = true;
  in.reason = "low-priority holder blocks higher-priority work on device memory";
  in.provenance = provenance;
  return in;
}

int main() {
  TEST_CONTEXT("priority-inversion");
  ReferenceAdapters ad;
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
  PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(1));
  rt.set_priority_generation(PriorityGeneration(1));

  // A low-priority holder blocks higher-priority work on device memory.
  auto ev = rt.priority_inversion_assessment(make_input(WorkloadId(101), WorkloadId(202),
                                                        ResourceClass::kDeviceMemory, PriorityGeneration(1),
                                                        PreemptibilityClass::kPreemptibleAtSafePoint, true,
                                                        PolicyGeneration(1), EvidenceKind::kMeasured));
  CHECK(ev.ok);
  CHECK(!ev.stale);
  CHECK(ev.resource == ResourceClass::kDeviceMemory);          // exact blocking resource
  CHECK(ev.holder_workload == WorkloadId(101));                // priority relationship
  CHECK(ev.blocked_workload == WorkloadId(202));
  CHECK(ev.holder_preemptible);
  CHECK_EQ(ev.blocking_duration_ns, 42);
  CHECK(ev.blocking_duration_measured);
  CHECK(ev.resolution == PriorityInversionResolution::kSafePreemptionResolves);
  CHECK(ev.positive_recommendation());                          // concrete + resolves
  CHECK(ev.provenance == EvidenceKind::kMeasured);

  // Stale priority generation is rejected.
  auto stale = rt.priority_inversion_assessment(make_input(WorkloadId(101), WorkloadId(202),
                                                           ResourceClass::kDeviceMemory, PriorityGeneration(0),
                                                           PreemptibilityClass::kPreemptibleAtSafePoint, true,
                                                           PolicyGeneration(1), EvidenceKind::kMeasured));
  CHECK(stale.stale);
  CHECK(!stale.ok);
  CHECK(!stale.positive_recommendation());

  // UNKNOWN preemptibility never becomes a positive recommendation.
  auto unknown = rt.priority_inversion_assessment(make_input(WorkloadId(101), WorkloadId(202),
                                                             ResourceClass::kDeviceMemory, PriorityGeneration(1),
                                                             PreemptibilityClass::kUnknown, true,
                                                             PolicyGeneration(1), EvidenceKind::kUnknown));
  CHECK(unknown.ok);
  CHECK(unknown.resolution == PriorityInversionResolution::kUnknown);
  CHECK(!unknown.positive_recommendation());
  CHECK(unknown.provenance == EvidenceKind::kUnknown);

  // A non-authoritative holder cannot resolve by preemption here.
  auto noauth = rt.priority_inversion_assessment(make_input(WorkloadId(101), WorkloadId(202),
                                                            ResourceClass::kDeviceMemory, PriorityGeneration(1),
                                                            PreemptibilityClass::kPreemptibleNow, false,
                                                            PolicyGeneration(1), EvidenceKind::kEstimated));
  CHECK(noauth.ok);
  CHECK(noauth.resolution == PriorityInversionResolution::kSafePreemptionDoesNotResolve);
  CHECK(!noauth.positive_recommendation());

  // A stale policy generation is rejected (invalid, not positive).
  auto badpol = rt.priority_inversion_assessment(make_input(WorkloadId(101), WorkloadId(202),
                                                            ResourceClass::kDeviceMemory, PriorityGeneration(1),
                                                            PreemptibilityClass::kPreemptibleNow, true,
                                                            PolicyGeneration(2), EvidenceKind::kMeasured));
  CHECK(!badpol.ok);
  CHECK(!badpol.positive_recommendation());

  PF_TEST_RETURN();
}
