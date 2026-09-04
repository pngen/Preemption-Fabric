#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

static PreemptionRequest make_req() {
  PreemptionRequest r;
  r.id = PreemptionRequestId(100); r.generation = PreemptionRequestGeneration(1);
  r.workload_id = WorkloadId(10); r.workload_generation = WorkloadGeneration(1);
  r.execution_id = ExecutionId(20); r.execution_generation = ExecutionGeneration(1);
  r.attempt_id = AttemptId(30); r.attempt_generation = AttemptGeneration(1);
  r.coordinator_epoch = CoordinatorEpoch(1); r.worker_id = WorkerId(40);
  r.worker_boot_id = WorkerBootId(50); r.policy_generation = PolicyGeneration(1);
  r.reason = PreemptionReason::kMigration;
  return r;
}

// Drive the runtime to a durable PREEMPTED state.
static void preempt_to_durable(PreemptionRuntime& rt, ReferenceAdapters& ad) {
  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
  SafePoint sp;
  sp.id = SafePointId(80); sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20); sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40); sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true; sp.checkpoint_required = true;
  rt.declare_safe_point(sp);
  rt.publish_progress(100, ProgressKind::kDurable);
  rt.request_preemption(make_req());
  rt.begin_quiesce();
  rt.reach_safe_point(SafePointId(80), 500, true);
  rt.begin_quiesce();
  QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
  q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
  rt.report_quiesced(q);
  rt.request_state_capture();
  rt.report_state_captured(*ad.last_capture());
  rt.request_release();
  rt.report_released();
  auto cp = rt.commit_preempted();
  (void)cp;
}

int main() {
  TEST_CONTEXT("migration");

  // A compatible destination environment.
  {
    ReferenceAdapters ad;
    for (auto c : ad.categories()) ad.set_quiesced(c, true);
    ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
    ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);
    DeviceCapability current{"gpu:dest", 1, 8ull * 1024 * 1024 * 1024, 120, true, true};
    DeviceCapability required{"gpu:dest", 1, 4ull * 1024 * 1024 * 1024, 120, true, true};
    ad.set_capability_context(current, required);

    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    preempt_to_durable(rt, ad);

    // Intentionally incompatible destination rejected before resume.
    DeviceCapability incompatible{"gpu:dest", 1, 1ull * 1024, 120, false, true};
    auto m1 = rt.evaluate_migration_destination(incompatible);
    CHECK(m1.outcome == MigrationCompatibilityOutcome::kIncompatibleCapability);
    CHECK(!m1.reasons.empty());

    // Stale destination capability generation rejected.
    DeviceCapability stale{"gpu:dest", 999, 8ull * 1024 * 1024 * 1024, 120, true, true};
    auto m2 = rt.evaluate_migration_destination(stale);
    CHECK(m2.outcome == MigrationCompatibilityOutcome::kStaleCapabilityGeneration);

    // A compatible destination is accepted.
    DeviceCapability compat{"gpu:dest", 1, 8ull * 1024 * 1024 * 1024, 120, true, true};
    auto m3 = rt.evaluate_migration_destination(compat);
    CHECK(m3.outcome == MigrationCompatibilityOutcome::kCompatible);
    CHECK(!m3.reasons.empty());

    // Resume proceeds only under fresh execution/resume authority.
    auto el = rt.request_resume();
    CHECK(el.outcome == ResumeOutcome::kResumeRevalidationRequired);
    auto rv = rt.revalidate();
    CHECK(rv.outcome == PreemptionOutcome::kSafePreemptionCompleted);
    auto rs = rt.restore();
    CHECK(rs.outcome == PreemptionOutcome::kSafePreemptionCompleted);
    auto rm = rt.confirm_resumed();
    CHECK(rm.outcome == PreemptionOutcome::kSafePreemptionCompleted);
    CHECK(rt.lifecycle() == LifecycleState::kResumed);
  }

  // Dependency invalidation blocks migration until revalidated.
  {
    ReferenceAdapters ad;
    for (auto c : ad.categories()) ad.set_quiesced(c, true);
    ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
    // A stale dependency.
    ad.set_resume_dependency(adapter::DependencyStatus::kStale, DependencyGeneration(1), true, false);
    DeviceCapability current{"gpu:dest", 1, 8ull * 1024 * 1024 * 1024, 120, true, true};
    DeviceCapability required{"gpu:dest", 1, 4ull * 1024 * 1024 * 1024, 120, true, true};
    ad.set_capability_context(current, required);

    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    preempt_to_durable(rt, ad);

    DeviceCapability compat{"gpu:dest", 1, 8ull * 1024 * 1024 * 1024, 120, true, true};
    auto m = rt.evaluate_migration_destination(compat);
    CHECK(m.outcome == MigrationCompatibilityOutcome::kBlockedDependency);

    // Supply fresh dependency/resource/policy evidence; then compatible again.
    ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(2), false, false);
    auto m2 = rt.evaluate_migration_destination(compat);
    CHECK(m2.outcome == MigrationCompatibilityOutcome::kCompatible);
  }

  PF_TEST_RETURN();
}
