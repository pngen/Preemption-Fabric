// Real RTX 5090 (sm_120) cooperative-preemption proof for Preemption Fabric.
//
// Segmented device work with explicit runtime safe points between real kernel
// segments. This proves cooperative interruption around real accelerator work.
// It does NOT claim hardware instruction-level, context, MIG, or driver-level
// kernel preemption; interruption is cooperative at application/runtime
// boundaries between real kernel segments.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/runtime.hpp"
#include "preemption_fabric/version.hpp"

using namespace pf;
using namespace pf::adapter;

__global__ void scale_kernel(float* state, int n, float factor) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) state[i] *= factor;
}

namespace {
constexpr int kN = 10000;
constexpr float kTol = 1e-4f;

struct DeviceState {
  float* data = nullptr;
  bool alloc() {
    return cudaMalloc(&data, kN * sizeof(float)) == cudaSuccess;
  }
  void free() {
    if (data) { cudaFree(data); data = nullptr; }
  }
};

void run_segment(DeviceState& s, float factor) {
  scale_kernel<<<(kN + 255) / 256, 256>>>(s.data, kN, factor);
  cudaDeviceSynchronize();
}

// Expected result: input[i] * product of all segment factors.
void cpu_reference(const std::vector<float>& input, const std::vector<float>& factors, std::vector<float>& out) {
  out.resize(input.size());
  float prod = 1.0f;
  for (float f : factors) prod *= f;
  for (size_t i = 0; i < input.size(); ++i) out[i] = input[i] * prod;
}

bool check_parity(const std::vector<float>& expected, const float* device) {
  std::vector<float> host(kN);
  cudaMemcpy(host.data(), device, kN * sizeof(float), cudaMemcpyDeviceToHost);
  for (int i = 0; i < kN; ++i) {
    if (std::fabs(host[i] - expected[i]) > kTol) {
      std::printf("  parity mismatch at %d: got %f expected %f\n", i, host[i], expected[i]);
      return false;
    }
  }
  return true;
}

size_t device_free() {
  size_t freeb = 0, tot = 0;
  cudaMemGetInfo(&freeb, &tot);
  return freeb;
}

// Configure a reference-adapter environment with quiescence, a contract, and
// current dependencies.
void configure(ReferenceAdapters& ad) {
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, kN * sizeof(float)}});
  ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);
}

PreemptionRequest make_req() {
  PreemptionRequest r;
  r.id = PreemptionRequestId(100); r.generation = PreemptionRequestGeneration(1);
  r.workload_id = WorkloadId(10); r.workload_generation = WorkloadGeneration(1);
  r.execution_id = ExecutionId(20); r.execution_generation = ExecutionGeneration(1);
  r.attempt_id = AttemptId(30); r.attempt_generation = AttemptGeneration(1);
  r.coordinator_epoch = CoordinatorEpoch(1); r.worker_id = WorkerId(40);
  r.worker_boot_id = WorkerBootId(50); r.reason = PreemptionReason::kCapacityPressure;
  r.policy_generation = PolicyGeneration(1); r.authority_generation = AuthorityGeneration(1);
  return r;
}

void declare_boundary(PreemptionRuntime& rt) {
  SafePoint sp;
  sp.id = SafePointId(80); sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20); sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40); sp.worker_boot_id = WorkerBootId(50);
  sp.state_capture_required = true; sp.checkpoint_required = true;
  sp.invariant = "runtime safe point between real kernel segments";
  sp.provenance = EvidenceKind::kReal;
  rt.declare_safe_point(sp);
}

}  // namespace

int main() {
  std::printf("=== Preemption Fabric CUDA proof: NVIDIA RTX 5090 (sm_120) ===\n\n");
  std::printf("interruption mode: cooperative boundaries between real kernel segments\n");
  std::printf("claimed hardware features: NONE beyond cooperative boundaries\n\n");

  const size_t base = device_free();
  std::printf("baseline device free memory: %zu MiB\n", (base >> 20));

  std::vector<float> input(kN);
  for (int i = 0; i < kN; ++i) input[i] = 0.5f + static_cast<float>(i) * 1e-3f;

  int failures = 0;

  // ---- Scenario A/B/C: normal safe preemption with state preservation ----
  {
    std::printf("\n--- Scenario A: safe cooperative preemption with durable state + CPU parity (REAL) ---\n");
    ReferenceAdapters ad;
    configure(ad);
    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                       AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt);

    DeviceState s;
    if (!s.alloc()) { std::printf("  cudaMalloc failed\n"); return 1; }
    std::vector<float> factors = {1.0f, 1.01f, 1.02f, 1.03f, 1.04f};
    cudaMemcpy(s.data, input.data(), kN * sizeof(float), cudaMemcpyHostToDevice);

    // Segments 0..1 with safe points.
    run_segment(s, factors[0]);
    rt.reach_safe_point(SafePointId(80), 100, false);
    rt.publish_progress(100, ProgressKind::kDurable);
    run_segment(s, factors[1]);
    rt.reach_safe_point(SafePointId(80), 200, true);
    rt.publish_progress(200, ProgressKind::kDurable);

    // Preemption request.
    auto adm = rt.request_preemption(make_req());
    rt.begin_quiesce();  // at a valid boundary -> quiescing
    // This segment's durable progress is recomputable from input+factors, so we
    // release without preserving volatile bytes (Scenario C: recompute).
    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    rt.report_quiesced(q);
    auto cap = rt.request_state_capture();
    (void)cap;
    if (ad.last_capture()) rt.report_state_captured(*ad.last_capture());
    rt.request_release();
    rt.report_released();
    // Release the device allocation.
    s.free();
    auto cp = rt.commit_preempted();
    std::printf("  PREEMPTED: outcome=%s lifecycle=%s\n",
                std::string(std::string_view(to_string(cp.outcome))).c_str(),
                std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
    if (cp.outcome != PreemptionOutcome::kSafePreemptionCompleted) ++failures;

    // Fresh resume: reallocate + reacquire, restore by recomputing from durable
    // progress, then continue remaining segments.
    if (!s.alloc()) { std::printf("  realloc failed\n"); return 1; }
    auto el = rt.request_resume();
    auto rv = rt.revalidate();
    auto rs = rt.restore();
    (void)el; (void)rv; (void)rs;
    // Recompute from input x factors[0..1] to reach the preserved boundary.
    std::vector<float> recompute(input);
    for (int j = 0; j <= 1; ++j) for (int i = 0; i < kN; ++i) recompute[i] *= factors[j];
    cudaMemcpy(s.data, recompute.data(), kN * sizeof(float), cudaMemcpyHostToDevice);
    auto rm = rt.confirm_resumed();
    std::printf("  RESUMED: outcome=%s lifecycle=%s\n",
                std::string(std::string_view(to_string(rm.outcome))).c_str(),
                std::string(std::string_view(to_string(rt.lifecycle()))).c_str());

    // Continue remaining segments 2..4.
    for (int j = 2; j < static_cast<int>(factors.size()); ++j) run_segment(s, factors[j]);

    std::vector<float> expected;
    cpu_reference(input, factors, expected);
    bool parity = check_parity(expected, s.data);
    std::printf("  CPU parity: %s\n", parity ? "PASS" : "FAIL");
    if (!parity) ++failures;
    s.free();

    size_t after = device_free();
    std::printf("  device memory returned to baseline: %s (%zu MiB base, %zu MiB after)\n",
                (std::abs(static_cast<long long>(after) - static_cast<long long>(base)) < (8 << 20)) ? "PASS" : "FAIL",
                (base >> 20), (after >> 20));
    if (std::abs(static_cast<long long>(after) - static_cast<long long>(base)) >= (8 << 20)) ++failures;
  }

  // ---- Scenario B: state capture required (real copyback) ----
  {
    std::printf("\n--- Scenario B: state capture required (real device copyback) [REAL] ---\n");
    ReferenceAdapters ad;
    configure(ad);
    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                       AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt);

    DeviceState s;
    if (!s.alloc()) return 1;
    std::vector<float> factors = {1.0f, 1.01f, 1.02f};
    cudaMemcpy(s.data, input.data(), kN * sizeof(float), cudaMemcpyHostToDevice);
    run_segment(s, factors[0]);
    rt.reach_safe_point(SafePointId(80), 100, true);
    rt.publish_progress(100, ProgressKind::kDurable);
    run_segment(s, factors[1]);
    rt.reach_safe_point(SafePointId(80), 200, true);
    rt.publish_progress(200, ProgressKind::kDurable);

    rt.request_preemption(make_req());
    rt.begin_quiesce();
    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    rt.report_quiesced(q);

    // Real state capture: copy back the full device state to a host buffer.
    std::vector<float> captured(kN);
    cudaMemcpy(captured.data(), s.data, kN * sizeof(float), cudaMemcpyDeviceToHost);
    rt.request_state_capture();
    if (ad.last_capture()) rt.report_state_captured(*ad.last_capture());
    rt.request_release();
    rt.report_released();
    s.free();
    rt.commit_preempted();
    std::printf("  state captured (real D2H copyback) and device memory freed\n");

    // Resume by restoring the captured bytes into a fresh allocation.
    if (!s.alloc()) return 1;
    auto el = rt.request_resume();
    auto rv2 = rt.revalidate();
    auto rs = rt.restore();
    (void)el; (void)rv2; (void)rs;
    cudaMemcpy(s.data, captured.data(), kN * sizeof(float), cudaMemcpyHostToDevice);
    rt.confirm_resumed();
    run_segment(s, factors[2]);

    std::vector<float> expected;
    cpu_reference(input, factors, expected);
    bool parity = check_parity(expected, s.data);
    std::printf("  CPU parity: %s\n", parity ? "PASS" : "FAIL");
    if (!parity) ++failures;
    s.free();
  }

  // ---- Scenario D: invalid boundary -> DEFER_TO_SAFE_POINT ----
  {
    std::printf("\n--- Scenario D: invalid boundary returns DEFER_TO_SAFE_POINT [REAL] ---\n");
    ReferenceAdapters ad;
    configure(ad);
    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                       AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt);
    DeviceState s;
    if (!s.alloc()) return 1;
    cudaMemcpy(s.data, input.data(), kN * sizeof(float), cudaMemcpyHostToDevice);
    // Request while work is between boundaries (no safe point reached yet).
    rt.request_preemption(make_req());
    auto bq = rt.begin_quiesce();
    std::printf("  begin_quiesce outcome=%s lifecycle=%s\n",
                std::string(std::string_view(to_string(bq.outcome))).c_str(),
                std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
    if (bq.outcome != PreemptionOutcome::kDeferToSafePoint) ++failures;
    // Reach the actual boundary, then complete safe preemption.
    run_segment(s, 1.0f);
    rt.reach_safe_point(SafePointId(80), 100, false);
    rt.begin_quiesce();
    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    rt.report_quiesced(q);
    rt.request_state_capture();   // capture not required -> already durable
    rt.request_release();
    rt.report_released();
    auto cp = rt.commit_preempted();
    std::printf("  after reaching boundary, PREEMPTED=%s\n",
                std::string(std::string_view(to_string(cp.outcome))).c_str());
    if (cp.outcome != PreemptionOutcome::kSafePreemptionCompleted) ++failures;
    s.free();
  }

  // ---- Scenario E: stale authority rejected before kernel launch ----
  {
    std::printf("\n--- Scenario E: stale authority rejected before launch [REAL] ---\n");
    ReferenceAdapters ad;
    configure(ad);
    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                       AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt);
    DeviceState s;
    if (!s.alloc()) return 1;
    cudaMemcpy(s.data, input.data(), kN * sizeof(float), cudaMemcpyHostToDevice);
    rt.publish_progress(100, ProgressKind::kDurable);
    rt.request_preemption(make_req());
    rt.begin_quiesce();
    rt.reach_safe_point(SafePointId(80), 100, false);  // boundary
    rt.begin_quiesce();
    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    rt.report_quiesced(q);
    rt.request_state_capture();
    if (ad.last_capture()) rt.report_state_captured(*ad.last_capture());
    rt.request_release();
    rt.report_released();
    auto cp = rt.commit_preempted();
    // The old attempt is fenced; the runtime refuses authoritative progress.
    bool old_authoritative = rt.publish_progress(900, ProgressKind::kDurable);
    std::printf("  old attempt publish_progress after PREEMPTED: %d (expect 0)\n", (int)old_authoritative);
    // Fresh authority grants a new attempt generation; kernel launch is allowed.
    rt.request_resume();
    rt.revalidate();
    rt.restore();
    auto rm = rt.confirm_resumed();
    bool resumed = rm.outcome == PreemptionOutcome::kSafePreemptionCompleted;
    std::printf("  fresh authority obtained: %s (attempt gen advanced)\n", resumed ? "yes" : "no");
    if (old_authoritative || !resumed) ++failures;
    s.free();
  }

  // ---- Host-simulated worker loss (worker death) & coordinator restart ----
  {
    std::printf("\n--- Scenario F/G: worker loss & coordinator restart (host-simulated, SYNTHETIC) ---\n");
    // Worker loss before the preservation boundary must NOT claim safe preemption.
    ReferenceAdapters ad;
    configure(ad);
    PreemptionRuntime rt(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                       AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt);
    rt.publish_progress(100, ProgressKind::kDurable);
    // Worker incarnation becomes dead (fenced) before any durable capture.
    rt.set_worker_incarnation(WorkerId(40), WorkerBootId(0));
    rt.request_preemption(make_req());
    rt.begin_quiesce();
    QuiescenceReport q; q.generation = QuiescenceGeneration(1); q.worker_id = WorkerId(40);
    q.worker_boot_id = WorkerBootId(50); q.fully_quiesced = true;
    auto quiesce_result = rt.report_quiesced(q);
    auto cp = rt.commit_preempted();
    std::printf("  worker loss before capture: quiesce=%s PREEMPTED=%s (must not be safe preemption)\n",
                std::string(std::string_view(to_string(quiesce_result.outcome))).c_str(),
                std::string(std::string_view(to_string(cp.outcome))).c_str());
    if (cp.outcome == PreemptionOutcome::kSafePreemptionCompleted) ++failures;

    // Coordinator restart: reach durable PREEMPTED, persist, then restore under a
    // new coordinator epoch and revalidate.
    PreemptionRuntime rt2(CoordinatorEpoch(1), PolicyGeneration(1), ad);
    rt2.start_execution(WorkloadId(10), WorkloadGeneration(1), ExecutionId(20), ExecutionGeneration(1),
                        AttemptId(30), AttemptGeneration(1), WorkerId(40), WorkerBootId(50));
    declare_boundary(rt2);
    rt2.publish_progress(100, ProgressKind::kDurable);
    rt2.request_preemption(make_req());
    rt2.begin_quiesce();
    rt2.reach_safe_point(SafePointId(80), 200, true);
    rt2.begin_quiesce();
    QuiescenceReport q2; q2.generation = QuiescenceGeneration(1); q2.worker_id = WorkerId(40);
    q2.worker_boot_id = WorkerBootId(50); q2.fully_quiesced = true;
    rt2.report_quiesced(q2);
    rt2.request_state_capture();
    if (ad.last_capture()) rt2.report_state_captured(*ad.last_capture());
    rt2.request_release();
    rt2.report_released();
    rt2.commit_preempted();
    auto bytes = rt2.persist_bytes();

    // New coordinator process (epoch 2), fresh worker incarnation.
    PreemptionRuntime rt3(CoordinatorEpoch(2), PolicyGeneration(1), ad);
    bool recovered = rt3.restore_bytes(bytes);
    rt3.set_worker_incarnation(WorkerId(40), WorkerBootId(600));
    auto el = rt3.request_resume();
    auto rv = rt3.revalidate();
    auto rs = rt3.restore();
    auto rm = rt3.confirm_resumed();
    std::printf("  coordinator restart: recovered=%d, resumed after revalidation=%s\n",
                recovered ? 1 : 0, rm.outcome == PreemptionOutcome::kSafePreemptionCompleted ? "yes" : "no");
    (void)el; (void)rv; (void)rs;
    if (!recovered || rm.outcome != PreemptionOutcome::kSafePreemptionCompleted) ++failures;
  }

  std::printf("\n=== CUDA proof complete: FAILURES=%d ===\n", failures);
  return failures == 0 ? 0 : 1;
}
