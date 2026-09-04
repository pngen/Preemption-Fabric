#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

static PreemptionRequest make_req(std::uint64_t rid, std::uint64_t rg, std::uint64_t eg, std::uint64_t ag,
                                  std::uint64_t epoch, std::uint64_t boot) {
  PreemptionRequest r;
  r.id = PreemptionRequestId(rid); r.generation = PreemptionRequestGeneration(rg);
  r.workload_id = WorkloadId(10); r.workload_generation = WorkloadGeneration(1);
  r.execution_id = ExecutionId(20); r.execution_generation = ExecutionGeneration(eg);
  r.attempt_id = AttemptId(30); r.attempt_generation = AttemptGeneration(ag);
  r.coordinator_epoch = CoordinatorEpoch(epoch); r.worker_id = WorkerId(40);
  r.worker_boot_id = WorkerBootId(boot); r.policy_generation = PolicyGeneration(1);
  r.authority_generation = AuthorityGeneration(1);
  return r;
}

int main() {
  TEST_CONTEXT("adversarial");
  ReferenceAdapters ad;
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
  ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);
  PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
  SafePoint sp;
  sp.id = SafePointId(80); sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20); sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40); sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true; sp.checkpoint_required = true;
  rt.declare_safe_point(sp);
  rt.publish_progress(100, ProgressKind::kDurable);

  // --- Stale authority attacks ---
  auto stale_epoch = rt.request_preemption(make_req(1, 1, 1, 1, 2, 50));
  CHECK(stale_epoch.code == RequestAdmissionCode::kRejectedStale);  // wrong epoch

  auto stale_gen = rt.request_preemption(make_req(1, 1, 1, 0, 1, 50));
  CHECK(stale_gen.code == RequestAdmissionCode::kRejectedStale);  // old attempt gen

  auto stale_boot = rt.request_preemption(make_req(1, 1, 1, 1, 1, 999));
  CHECK(stale_boot.code == RequestAdmissionCode::kRejectedStale);  // old boot

  // --- Admission then conflicting duplicate rejected ---
  auto good = rt.request_preemption(make_req(100, 1, 1, 1, 1, 50));
  CHECK(good.code == RequestAdmissionCode::kAccepted);
  auto dup = rt.request_preemption(make_req(101, 2, 1, 1, 1, 50));
  CHECK(dup.code == RequestAdmissionCode::kConflictingDuplicate);

  // --- Forged safe point rejected ---
  bool forged = false;
  try { rt.reach_safe_point(SafePointId(999), 100, false); } catch (const PreemptionError&) { forged = true; }
  CHECK(forged);

  // --- Backward progress rejected (attempt is not yet authoritative) ---
  // (attempt_authoritative is false until begin_execution grants it below)
  CHECK(!rt.publish_progress(50, ProgressKind::kDurable));

  // --- Release before quiescence / durability rejected ---
  auto rel_bad = rt.request_release();  // lifecycle is Assessing -> cannot release
  CHECK(rel_bad.outcome == PreemptionOutcome::kCannotPreemptSafely || rel_bad.outcome == PreemptionOutcome::kPreemptionFailed);

  // --- PREEMPTED before release rejected ---
  auto pre_bad = rt.commit_preempted();
  CHECK(pre_bad.outcome != PreemptionOutcome::kSafePreemptionCompleted);

  // --- Resume before PREEMPTED rejected ---
  auto res_bad = rt.request_resume();
  CHECK(res_bad.outcome == ResumeOutcome::kResumeNotAllowed);

  // --- A fully valid preemption path to PREEMPTED (quiesce+release+commit) ---
  rt.begin_quiesce();                     // Assessing -> WaitingForSafePoint (defer)
  rt.reach_safe_point(SafePointId(80), 500, true);  // reach boundary
  rt.begin_quiesce();                     // -> Quiescing
  QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
  q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
  rt.report_quiesced(q);                  // -> Quiesced
  rt.request_state_capture();            // -> CapturingState
  rt.report_state_captured(*ad.last_capture());  // -> StateDurable
  rt.request_release();                  // -> Releasing
  rt.report_released();                  // -> ResourcesReleased
  auto cp = rt.commit_preempted();       // -> Preempted
  CHECK(cp.outcome == PreemptionOutcome::kSafePreemptionCompleted);
  CHECK(rt.lifecycle() == LifecycleState::kPreempted);

  // --- Stale checkpoint generation blocks revalidation ---
  rt.request_resume();
  ad.set_current_checkpoint_generation(CheckpointGeneration(99));  // supersede
  auto rv = rt.revalidate();
  CHECK(rv.outcome == PreemptionOutcome::kPreemptionFailed || rv.outcome == PreemptionOutcome::kCannotPreemptSafely);

  // --- Cancellation blocks stale resume ---
  const std::uint32_t restart = 0;  // markers
  (void)restart;

  // Two runtimes to test cancel-vs-stale-resume precedence.
  {
    ReferenceAdapters ad2;
    for (auto c : ad2.categories()) ad2.set_quiesced(c, true);
    ad2.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
    PreemptionRuntime rt2(CoordinatorEpoch(1), PolicyGeneration(1), ad2);
    rt2.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                        AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    SafePoint sp2;
    sp2.id = SafePointId(80); sp2.generation = SafePointGeneration(1);
    sp2.execution_id = ExecutionId(20); sp2.attempt_id = AttemptId(30);
    sp2.worker_id = WorkerId(40); sp2.worker_boot_id = WorkerBootId(50);
    sp2.state_capture_required = true; sp2.checkpoint_required = true;
    rt2.declare_safe_point(sp2);
    rt2.request_preemption(make_req(1, 1, 1, 1, 1, 50));
    rt2.begin_quiesce();
    rt2.reach_safe_point(SafePointId(80), 500, true);
    rt2.begin_quiesce();
    QuiescenceReport q2; q2.generation = QuiescenceGeneration(1); q2.worker_id = WorkerId(40);
    q2.worker_boot_id = WorkerBootId(50); q2.fully_quiesced = true;
    rt2.report_quiesced(q2);
    rt2.request_state_capture();
    rt2.report_state_captured(*ad2.last_capture());
    rt2.request_release();
    rt2.report_released();
    rt2.commit_preempted();
    rt2.request_resume();
    rt2.cancel();
    // A cancelled workload must not be RESUME_READY.
    CHECK(rt2.lifecycle() == LifecycleState::kCancelled);
  }

  PF_TEST_RETURN();
}
