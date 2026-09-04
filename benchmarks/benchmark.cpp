// Preemption Fabric benchmarks. Each reports completed operations per second.
// Timings use a steady clock and are labeled MEASURED; no precision is invented.
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/persistence/store.hpp"
#include "preemption_fabric/protocol/framing.hpp"
#include "preemption_fabric/runtime.hpp"

using namespace pf;
using namespace pf::adapter;
using Clock = std::chrono::steady_clock;

static double report(const char* name, std::uint64_t iters, Clock::time_point start, Clock::time_point end) {
  double sec = std::chrono::duration<double>(end - start).count();
  double ops = sec > 0 ? static_cast<double>(iters) / sec : 0.0;
  std::printf("%-36s %10llu iterations  %8.3f s  %12.0f ops/s\n", name, (unsigned long long)iters, sec, ops);
  return ops;
}

int main() {
  const std::uint64_t kIters = 100000;

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

  // Request admission (idempotent duplicate after first).
  PreemptionRequest req;
  req.id = PreemptionRequestId(1); req.generation = PreemptionRequestGeneration(1);
  req.execution_id = ExecutionId(20); req.execution_generation = ExecutionGeneration(1);
  req.attempt_id = AttemptId(30); req.attempt_generation = AttemptGeneration(1);
  req.coordinator_epoch = CoordinatorEpoch(1); req.worker_id = WorkerId(40);
  req.worker_boot_id = WorkerBootId(50); req.policy_generation = PolicyGeneration(1);
  rt.request_preemption(req);

  {
    auto s = Clock::now();
    for (std::uint64_t i = 0; i < kIters; ++i) { auto a = rt.request_preemption(req); (void)a; }
    report("request admission", kIters, s, Clock::now());
  }
  {
    auto s = Clock::now();
    std::uint64_t n = 0;
    for (std::uint64_t i = 0; i < kIters; ++i) { (void)rt.publish_progress(i, ProgressKind::kDurable); ++n; (void)rt.publish_progress(0, ProgressKind::kDurable); }
    if (n > 0) report("progress publication", n, s, Clock::now());
  }
  {
    auto s = Clock::now();
    for (std::uint64_t i = 0; i < kIters; ++i) { (void)rt.assess(); }
    report("preemptibility assessment", kIters, s, Clock::now());
  }
  {
    auto bytes = rt.persist_bytes();
    auto s = Clock::now();
    std::uint64_t ok = 0;
    for (std::uint64_t i = 0; i < 20000; ++i) { auto d = pf::persist::decode(bytes); if (d.ok) ++ok; }
    report("persistence decode", 20000, s, Clock::now());
    (void)ok;
  }
  {
    proto::Message m;
    m.type = proto::MessageType::kPublishProgress; m.progress_position = 42; m.detail = "benchmark";
    auto frame = proto::encode_frame(m);
    auto s = Clock::now();
    for (std::uint64_t i = 0; i < kIters; ++i) { (void)proto::encode_frame(m); (void)proto::decode_frame(frame); }
    report("protocol encode+decode", kIters, s, Clock::now());
  }
  {
    // Concurrent read-heavy assessment.
    const int threads = 8;
    const std::uint64_t per = 20000;
    auto s = Clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) ts.emplace_back([&] { for (std::uint64_t i = 0; i < per; ++i) (void)rt.assess(); });
    for (auto& t : ts) t.join();
    report("concurrent assessment (8 threads)", per * threads, s, Clock::now());
  }
  return 0;
}
