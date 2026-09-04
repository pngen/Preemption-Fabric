// Independent downstream consumer of the installed PreemptionFabric package.
// Exercises the real public API against the installed headers + library.
#include <cstdio>
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

int main() {
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

  PreemptionRequest req;
  req.id = PreemptionRequestId(1); req.generation = PreemptionRequestGeneration(1);
  req.execution_id = ExecutionId(20); req.execution_generation = ExecutionGeneration(1);
  req.attempt_id = AttemptId(30); req.attempt_generation = AttemptGeneration(1);
  req.coordinator_epoch = CoordinatorEpoch(1); req.worker_id = WorkerId(40);
  req.worker_boot_id = WorkerBootId(50); req.policy_generation = PolicyGeneration(1);
  auto adm = rt.request_preemption(req);
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
  std::printf("downstream consumer: request=%s preempted=%s lifecycle=%s durable=%llu\n",
              adm.code == RequestAdmissionCode::kAccepted ? "accepted" : "rejected",
              std::string(std::string_view(to_string(cp.outcome))).c_str(),
              std::string(std::string_view(to_string(rt.lifecycle()))).c_str(),
              (unsigned long long)rt.durable_progress());
  return cp.outcome == PreemptionOutcome::kSafePreemptionCompleted ? 0 : 1;
}
