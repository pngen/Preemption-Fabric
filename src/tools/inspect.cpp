// Preemption Fabric inspection CLI.
//
// Dumps the inspectable state of a preemption runtime (in-process scenario or a
// persisted state file): active preemption state, authoritative request
// generation, current safe point, durable progress, preservation/release
// obligations, resume blockers, and explanations.
#include <cstdio>
#include <cstring>
#include <string>

#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

static void dump(const PreemptionRuntime& rt, const char* kind) {
  std::printf("=== Preemption Fabric inspection (%s) ===\n", kind);
  std::printf("lifecycle: %s\n", std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
  std::printf("preemptibility: %s\n", std::string(std::string_view(to_string(rt.current_preemptibility()))).c_str());
  std::printf("durable progress: %llu\n", (unsigned long long)rt.durable_progress());
  std::printf("current position: %llu\n", (unsigned long long)rt.current_position());
  std::printf("attempt authoritative: %s\n", rt.is_attempt_authoritative() ? "yes" : "no");
  std::printf("authoritative request generation: %llu\n",
              (unsigned long long)rt.authoritative_request_generation().value());
  if (auto sp = rt.current_safe_point()) {
    std::printf("current safe point: id=%llu gen=%llu pos=%llu capture=%s\n",
                (unsigned long long)sp->id.value(), (unsigned long long)sp->generation.value(),
                (unsigned long long)sp->progress_position, sp->state_capture_required ? "yes" : "no");
  } else {
    std::printf("current safe point: (none)\n");
  }
  std::printf("safe points declared: %zu\n", rt.safe_point_count());
  std::printf("--- state evidence ---\n");
  for (auto& e : rt.explain_state()) std::printf("  %s: %s\n", e.message.c_str(), e.detail.c_str());
  std::printf("--- why not preemptible ---\n");
  for (auto& e : rt.explain_why_not_preemptible()) std::printf("  %s (%s): %s\n", e.message.c_str(),
             std::string(std::string_view(to_string(e.provenance))).c_str(), e.detail.c_str());
  std::printf("--- resume blockers ---\n");
  for (auto& e : rt.explain_resume_blockers()) std::printf("  %s: %s\n", e.message.c_str(), e.detail.c_str());
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string mode = "scenario";
  std::string state_file;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) { mode = "inspect"; state_file = argv[++i]; }
    else if (std::strcmp(argv[i], "--scenario") == 0) mode = "scenario";
  }

  ReferenceAdapters ad;
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
  ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);
  PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);

  if (mode == "inspect") {
    if (rt.load(state_file)) dump(rt, "persisted state");
    else std::printf("could not load state file %s\n", state_file.c_str());
    return 0;
  }

  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
  SafePoint sp;
  sp.id = SafePointId(80); sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20); sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40); sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true; sp.checkpoint_required = true;
  sp.invariant = "boundary after segment 3"; sp.provenance = EvidenceKind::kReal;
  rt.declare_safe_point(sp);
  rt.publish_progress(100, ProgressKind::kDurable);
  dump(rt, "running");

  PreemptionRequest req;
  req.id = PreemptionRequestId(100); req.generation = PreemptionRequestGeneration(1);
  req.workload_id = WorkloadId(10); req.workload_generation = WorkloadGeneration(1);
  req.execution_id = ExecutionId(20); req.execution_generation = ExecutionGeneration(1);
  req.attempt_id = AttemptId(30); req.attempt_generation = AttemptGeneration(1);
  req.coordinator_epoch = CoordinatorEpoch(1); req.worker_id = WorkerId(40);
  req.worker_boot_id = WorkerBootId(50); req.policy_generation = PolicyGeneration(1);
  req.authority_generation = AuthorityGeneration(1);
  rt.request_preemption(req);
  dump(rt, "requested");
  return 0;
}
