#include <vector>
#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"
#include "preemption_fabric/version.hpp"

using namespace pf;
using namespace pf::adapter;

// Configure the reference adapters for a deterministic, fully-quiesced,
// capture-capable, resource-bearing execution.
static void configure(ReferenceAdapters& ad) {
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1),
                  {{ResourceClass::kDeviceMemory, 100}});
  ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);
  ad.set_authoritative(ExecutionId(20), AttemptId(30), ExecutionGeneration(1), AttemptGeneration(1),
                       CoordinatorEpoch(1), WorkerBootId(50));
}

static PreemptionRequest make_request(WorkloadId w, ExecutionId e, ExecutionGeneration eg, AttemptId a,
                                      AttemptGeneration ag, CoordinatorEpoch epoch, WorkerBootId boot,
                                      PreemptionRequestId rid, PreemptionRequestGeneration rg) {
  PreemptionRequest req;
  req.id = rid;
  req.generation = rg;
  req.workload_id = w;
  req.workload_generation = WorkloadGeneration(1);
  req.execution_id = e;
  req.execution_generation = eg;
  req.attempt_id = a;
  req.attempt_generation = ag;
  req.coordinator_epoch = epoch;
  req.worker_id = WorkerId(40);
  req.worker_boot_id = boot;
  req.reason = PreemptionReason::kCapacityPressure;
  req.policy_generation = PolicyGeneration(1);
  req.authority_generation = AuthorityGeneration(1);
  return req;
}

int main() {
  TEST_CONTEXT("runtime");

  ReferenceAdapters ad;
  configure(ad);
  PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));

  // Declare a safe point that requires state capture.
  SafePoint sp;
  sp.id = SafePointId(80);
  sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20);
  sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40);
  sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true;
  sp.checkpoint_required = true;
  sp.resume_requirement = true;
  sp.invariant = "boundary after segment 3";
  sp.provenance = EvidenceKind::kReal;
  rt.declare_safe_point(sp);

  // Publish durable progress.
  CHECK(rt.publish_progress(100, ProgressKind::kDurable));
  CHECK_EQ(rt.durable_progress(), 100);

  // Admit a valid preemption request.
  auto req = make_request(WorkloadId(10), ExecutionId(20), ExecutionGeneration(1), AttemptId(30),
                          AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(50),
                          PreemptionRequestId(100), PreemptionRequestGeneration(1));
  auto adm = rt.request_preemption(req);
  CHECK(adm.code == RequestAdmissionCode::kAccepted);

  // Assessment: work is between safe point boundaries.
  auto as = rt.assess();
  CHECK(as.preemptibility == PreemptibilityClass::kPreemptibleAtSafePoint);

  // Defer to the safe point.
  auto bq = rt.begin_quiesce();
  CHECK(bq.outcome == PreemptionOutcome::kDeferToSafePoint);
  CHECK(rt.lifecycle() == LifecycleState::kWaitingForSafePoint);

  // Worker reaches the next safe point.
  rt.reach_safe_point(SafePointId(80), 500, true);
  CHECK(rt.lifecycle() == LifecycleState::kSafePointReached);

  auto bq2 = rt.begin_quiesce();
  CHECK(bq2.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kQuiescing);

  // Quiesce.
  QuiescenceReport report;
  report.generation = QuiescenceGeneration(1);
  report.worker_id = WorkerId(40);
  report.worker_boot_id = WorkerBootId(50);
  report.fully_quiesced = true;
  report.provenance = EvidenceKind::kReal;
  auto q = rt.report_quiesced(report);
  CHECK(q.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kQuiesced);

  // Stale worker boot must not be able to publish quiescence.
  QuiescenceReport stale = report;
  stale.worker_boot_id = WorkerBootId(999);
  auto qs = rt.report_quiesced(stale);
  CHECK(qs.outcome == PreemptionOutcome::kPreemptionFailed);

  // Capture state.
  auto cap = rt.request_state_capture();
  CHECK(cap.outcome == PreemptionOutcome::kStateCaptureRequired);
  CHECK(rt.lifecycle() == LifecycleState::kCapturingState);
  auto cc = rt.report_state_captured(*ad.last_capture());
  CHECK(cc.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kStateDurable);

  // Release.
  auto rel = rt.request_release();
  CHECK(rel.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kReleasing);
  auto rr = rt.report_released();
  CHECK(rr.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kResourcesReleased);

  // Commit PREEMPTED.
  auto cp = rt.commit_preempted();
  CHECK(cp.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kPreempted);

  // Verify old execution is fenced: progress can no longer publish.
  CHECK(!rt.publish_progress(600, ProgressKind::kDurable));

  // Replaying the old preemption request is idempotent, not a mutation.
  auto dup = rt.request_preemption(req);
  CHECK(dup.code == RequestAdmissionCode::kIdempotentDuplicate);

  // A conflicting duplicate (newer generation, different id) is rejected.
  auto conflict = make_request(WorkloadId(10), ExecutionId(20), ExecutionGeneration(1), AttemptId(30),
                               AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(50),
                               PreemptionRequestId(200), PreemptionRequestGeneration(2));
  auto cdup = rt.request_preemption(conflict);
  CHECK(cdup.code == RequestAdmissionCode::kConflictingDuplicate);

  // A stale request from an older attempt generation is rejected.
  auto stale_request = make_request(WorkloadId(10), ExecutionId(20), ExecutionGeneration(1), AttemptId(30),
                                     AttemptGeneration(0), CoordinatorEpoch(1), WorkerBootId(50),
                                     PreemptionRequestId(300), PreemptionRequestGeneration(3));
  auto sadm = rt.request_preemption(stale_request);
  CHECK(sadm.code == RequestAdmissionCode::kRejectedStale);

  // Fresh resume.
  auto el = rt.request_resume();
  CHECK(el.outcome == ResumeOutcome::kResumeRevalidationRequired);
  CHECK(rt.lifecycle() == LifecycleState::kResumePending);

  auto rv = rt.revalidate();
  CHECK(rv.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kRestoring);

  auto rs = rt.restore();
  CHECK(rs.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kResumeReady);

  auto rm = rt.confirm_resumed();
  CHECK(rm.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kResumed);

  // Fresh authority: progress continues from the preserved boundary, not zero.
  CHECK(rt.publish_progress(600, ProgressKind::kDurable));
  CHECK_EQ(rt.durable_progress(), 600);

  // Persistence round-trip after a preempted state.
  auto bytes = rt.persist_bytes();
  PreemptionRuntime rt2(CoordinatorEpoch(2), PolicyGeneration(1), ad);
  CHECK(rt2.restore_bytes(bytes));
  CHECK(rt2.lifecycle() == LifecycleState::kResumed);

  PF_TEST_RETURN();
}
