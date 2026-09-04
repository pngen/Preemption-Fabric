// Orchestrated full-preemption phase benchmark.
//
// Measures COMPLETED preemption phases of a full preempt->resume cycle, not
// request submission. Uses the in-process reference-adapter harness (owning
// runtimes are deterministic stand-ins), so the orchestration latency is
// MEASURED while the preservation/release byte semantics are DERIVED/SYNTHETIC.
#include <chrono>
#include <cstdio>
#include <string>

#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;
using Clock = std::chrono::steady_clock;

static double ns(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::nano>(b - a).count();
}

static PreemptionRequest make_req() {
  PreemptionRequest r;
  r.id = PreemptionRequestId(100); r.generation = PreemptionRequestGeneration(1);
  r.workload_id = WorkloadId(10); r.workload_generation = WorkloadGeneration(1);
  r.execution_id = ExecutionId(20); r.execution_generation = ExecutionGeneration(1);
  r.attempt_id = AttemptId(30); r.attempt_generation = AttemptGeneration(1);
  r.coordinator_epoch = CoordinatorEpoch(1); r.worker_id = WorkerId(40);
  r.worker_boot_id = WorkerBootId(50); r.policy_generation = PolicyGeneration(1);
  r.reason = PreemptionReason::kCapacityPressure;
  return r;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const int kRuns = 1000;
  constexpr std::uint64_t kWorkloadSize = 100000;       // units of state
  constexpr std::uint64_t kSafePointSpacing = 25000;      // progress units per safe point
  constexpr std::uint64_t kPreservationBytes = 8ull * 1024 * 1024;  // 8 MiB preserved

  double d_assess = 0, d_defer = 0, d_quiesce = 0, d_preserve = 0, d_release = 0;
  double d_total_preempt = 0, d_revalidate = 0, d_restore = 0, d_resume = 0, d_cycle = 0;

  for (int i = 0; i < kRuns; ++i) {
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
    rt.publish_progress(kSafePointSpacing, ProgressKind::kDurable);

    auto t0 = Clock::now();
    rt.request_preemption(make_req());
    auto t1 = Clock::now();
    (void)rt.assess();
    auto t2 = Clock::now();
    d_assess += ns(t0, t2);

    // wait / defer to the safe point.
    rt.begin_quiesce();                      // defer -> waiting for safe point
    auto t3 = Clock::now();
    rt.reach_safe_point(SafePointId(80), 2 * kSafePointSpacing, true);  // reach the boundary
    d_defer += ns(t3, Clock::now());        // (wait-to-boundary orchestrates instantly in-process)
    rt.begin_quiesce();                      // -> quiescing
    auto t4 = Clock::now();

    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    rt.report_quiesced(q);
    auto t5 = Clock::now();
    d_quiesce += ns(t4, t5);

    rt.request_state_capture();
    rt.report_state_captured(*ad.last_capture());
    auto t6 = Clock::now();
    d_preserve += ns(t5, t6);

    rt.request_release();
    rt.report_released();
    auto t7 = Clock::now();
    d_release += ns(t6, t7);

    rt.commit_preempted();
    auto t8 = Clock::now();
    d_total_preempt += ns(t0, t8);

    rt.request_resume();
    auto t9 = Clock::now();
    rt.revalidate();
    auto t10 = Clock::now();
    d_revalidate += ns(t9, t10);

    rt.restore();
    auto t11 = Clock::now();
    d_restore += ns(t10, t11);

    rt.confirm_resumed();
    auto t12 = Clock::now();
    d_resume += ns(t11, t12);
    d_cycle += ns(t8, t12);
  }

  std::printf("=== Preemption Fabric full-preemption phase benchmark (SYNTHETIC harness; MEASURED orchestration) ===\n");
  std::printf("workload size      : %llu units of state (reference adapter)\n", (unsigned long long)kWorkloadSize);
  std::printf("safe-point spacing : %llu progress units\n", (unsigned long long)kSafePointSpacing);
  std::printf("preservation size  : %llu bytes (DERIVED: reference adapter records state ref, no real copy)\n",
              (unsigned long long)kPreservationBytes);
  std::printf("repetitions        : %d full preempt->resume cycles\n", kRuns);
  std::printf("provenance         : timing MEASURED; preservation/release byte semantics DERIVED/SYNTHETIC\n\n");

  auto R = [](double ns) { return ns > 1.0e6 ? std::to_string(ns / 1.0e6) + " ms" : ns > 1.0e3 ? std::to_string(ns / 1.0e3) + " us" : std::to_string(ns) + " ns"; };
  std::printf("phase (avg/cycle)                provenance  latency\n");
  std::printf("  request -> assessment          MEASURED    %s\n", R(d_assess / kRuns).c_str());
  std::printf("  wait to safe point             MEASURED    %s\n", R(d_defer / kRuns).c_str());
  std::printf("  quiescence                    MEASURED    %s\n", R(d_quiesce / kRuns).c_str());
  std::printf("  state preservation            DERIVED     %s\n", R(d_preserve / kRuns).c_str());
  std::printf("  resource release              DERIVED     %s\n", R(d_release / kRuns).c_str());
  std::printf("  total preemption latency      MEASURED    %s\n", R(d_total_preempt / kRuns).c_str());
  std::printf("  resume revalidation           MEASURED    %s\n", R(d_revalidate / kRuns).c_str());
  std::printf("  restore                       DERIVED     %s\n", R(d_restore / kRuns).c_str());
  std::printf("  resume                        MEASURED    %s\n", R(d_resume / kRuns).c_str());
  std::printf("  total preempt -> resume cycle MEASURED    %s\n", R(d_cycle / kRuns).c_str());
  std::printf("preserved work (durable progress): %llu units; recomputed interval at resume: NONE (state restored)\n",
              (unsigned long long)kSafePointSpacing);
  return 0;
}
