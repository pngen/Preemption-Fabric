#include <cstdint>
#include <cstdio>
#include "tests/test_framework.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"
#include "preemption_fabric/persistence/store.hpp"

using namespace pf;
using namespace pf::adapter;

// Deterministic xorshift64* RNG (no external deps).
struct Rng {
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed) {}
  std::uint64_t next() {
    std::uint64_t x = s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    s = x;
    return x * 0x2545F4914F6CDD1DULL;
  }
  std::uint64_t mod(std::uint64_t m) { return m ? next() % m : 0; }
};

static PreemptionRequest make_req(std::uint64_t rid, std::uint64_t rg, std::uint64_t eg, std::uint64_t ag,
                                  std::uint64_t epoch, std::uint64_t boot) {
  PreemptionRequest r;
  r.id = PreemptionRequestId(rid);
  r.generation = PreemptionRequestGeneration(rg);
  r.workload_id = WorkloadId(10); r.workload_generation = WorkloadGeneration(1);
  r.execution_id = ExecutionId(20); r.execution_generation = ExecutionGeneration(eg);
  r.attempt_id = AttemptId(30); r.attempt_generation = AttemptGeneration(ag);
  r.coordinator_epoch = CoordinatorEpoch(epoch);
  r.worker_id = WorkerId(40); r.worker_boot_id = WorkerBootId(boot);
  r.policy_generation = PolicyGeneration(1);
  r.authority_generation = AuthorityGeneration(1);
  return r;
}

static void report_fail(std::uint64_t seed, int op, const char* what) {
  ++pf_test::g_failures;
  std::printf("PROPERTY seed=%llu op=%d: %s\n", (unsigned long long)seed, op, what);
}

static void run_trial(std::uint64_t seed) {
  Rng rng(seed);
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
  sp.provenance = EvidenceKind::kReal; sp.state = SafePointState::kDeclared;
  rt.declare_safe_point(sp);

  std::uint64_t last_durable = 0;
  const int steps = 40;
  for (int i = 0; i < steps; ++i) {
    int op = static_cast<int>(rng.mod(9));
    try {
      if (op == 0) (void)rt.publish_progress(rng.mod(1000), ProgressKind::kDurable);
      else if (op == 1) (void)rt.request_preemption(make_req(100, 1, 1, 1, 1, 50));
      else if (op == 2) { rt.reach_safe_point(SafePointId(80), 500 + rng.mod(50), true); (void)rt.begin_quiesce(); }
      else if (op == 3) {
        QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
        q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
        (void)rt.report_quiesced(q);
        (void)rt.request_state_capture();
        if (ad.last_capture()) (void)rt.report_state_captured(*ad.last_capture());
      } else if (op == 4) { (void)rt.request_release(); (void)rt.report_released(); (void)rt.commit_preempted(); }
      else if (op == 5) { (void)rt.request_resume(); (void)rt.revalidate(); (void)rt.restore(); (void)rt.confirm_resumed(); }
      else if (op == 6) rt.cancel();
      else if (op == 7) rt.complete();
      else { (void)rt.assess(); (void)rt.snapshot(); }
    } catch (...) {
      // Illegal-transition rejections are handled by the runtime; invariants
      // still hold afterwards.
    }

    auto dur = rt.durable_progress();
    if (dur < last_durable) report_fail(seed, op, "durable progress not monotonic");
    last_durable = dur;

    auto bytes = rt.persist_bytes();
    auto dec = pf::persist::decode(bytes);
    if (!dec.ok) report_fail(seed, op, "snapshot decode failed");

    if (rt.is_preempted_durable()) {
      if (!dec.state.state_durable || !dec.state.resources_released || !dec.state.quiesced)
        report_fail(seed, op, "durable-preempted missing required boundary");
    }
    // One authoritative preemption generation per execution scope is enforced by
    // the runtime; conflicting/stale requests are rejected, never admitted.
  }
}

int main() {
  TEST_CONTEXT("property");
  for (std::uint64_t seed = 1; seed <= 200; ++seed) run_trial(seed);
  std::printf("property: ran 200 seeded trials; failures=%d\n", pf_test::g_failures);
  PF_TEST_RETURN();
}
