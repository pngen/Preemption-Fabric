#include <atomic>
#include <thread>
#include <vector>
#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/persistence/store.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;

// Genuine concurrency: many threads perform mutations and read-heavy queries on
// the SAME runtime concurrently. The runtime guards all mutable state; the
// reference adapters are only ever touched while the runtime lock is held, so
// they are serialized. No arbitrary sleeps are used as the concurrency proof.
int main() {
  TEST_CONTEXT("concurrency");
  const int kThreads = 16;
  const int kIters = 2000;

  ReferenceAdapters ad;
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
  rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                     AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
  SafePoint sp;
  sp.id = SafePointId(80); sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20); sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40); sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true; sp.checkpoint_required = true;
  rt.declare_safe_point(sp);

  PreemptionRequest req;
  req.id = PreemptionRequestId(100); req.generation = PreemptionRequestGeneration(1);
  req.workload_id = WorkloadId(10); req.workload_generation = WorkloadGeneration(1);
  req.execution_id = ExecutionId(20); req.execution_generation = ExecutionGeneration(1);
  req.attempt_id = AttemptId(30); req.attempt_generation = AttemptGeneration(1);
  req.coordinator_epoch = CoordinatorEpoch(1); req.worker_id = WorkerId(40);
  req.worker_boot_id = WorkerBootId(50); req.policy_generation = PolicyGeneration(1);
  req.authority_generation = AuthorityGeneration(1);

  std::atomic<int> failures{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kIters; ++i) {
        try {
          switch ((t + i) % 8) {
            case 0: (void)rt.publish_progress(static_cast<std::uint64_t>(i), ProgressKind::kDurable); break;
            case 1: (void)rt.request_preemption(req); break;
            case 2: (void)rt.assess(); break;
            case 3: (void)rt.snapshot(); break;
            case 4: (void)rt.explain_state(); break;
            case 5: (void)rt.current_preemptibility(); break;
            case 6: rt.cancel(); break;
            case 7: rt.complete(); break;
            default: break;
          }
        } catch (...) { ++failures; }
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(failures.load(), 0);

  // After the storm, the runtime must still be internally consistent.
  auto bytes = rt.persist_bytes();
  auto dec = pf::persist::decode(bytes);
  CHECK(dec.ok);
  std::printf("concurrency: %d threads x %d iters; exceptions=%d\n", kThreads, kIters, failures.load());
  PF_TEST_RETURN();
}
