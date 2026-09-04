// Forced-termination distinction: a process death (or a failed/quarantined
// boundary) must never be reported as a successful safe preemption.
#include <cstdio>
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

int main() {
  ReferenceAdapters ad;
  for (auto c : ad.categories()) ad.set_quiesced(c, false);  // NOT quiesced
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
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

  PreemptionRequest req;
  req.id = PreemptionRequestId(1); req.generation = PreemptionRequestGeneration(1);
  req.execution_id = ExecutionId(20); req.execution_generation = ExecutionGeneration(1);
  req.attempt_id = AttemptId(30); req.attempt_generation = AttemptGeneration(1);
  req.coordinator_epoch = CoordinatorEpoch(1); req.worker_id = WorkerId(40);
  req.worker_boot_id = WorkerBootId(50); req.policy_generation = PolicyGeneration(1);
  rt.request_preemption(req);
  rt.begin_quiesce();                                   // defer to safe point
  rt.reach_safe_point(SafePointId(80), 500, true);      // reach the boundary
  rt.begin_quiesce();                                   // -> Quiescing

  // "Process death": quiescence never completes (required activity not quiesced);
  // the runtime must NOT reach PREEMPTED and must not claim preserved progress.
  QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
  q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = false;
  auto qr = rt.report_quiesced(q);
  auto cp = rt.commit_preempted();

  std::printf("forced termination: quiesce outcome=%s, PREEMPTED outcome=%s, lifecycle=%s\n",
              std::string(std::string_view(to_string(qr.outcome))).c_str(),
              std::string(std::string_view(to_string(cp.outcome))).c_str(),
              std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
  std::printf("safe preemption claimed? %s\n", rt.lifecycle() == LifecycleState::kPreempted ? "YES (BUG)" : "no (correct)");
  return 0;
}
