#include <vector>
#include "tests/test_framework.hpp"
#include "preemption_fabric/persistence/store.hpp"
#include "preemption_fabric/version.hpp"

using namespace pf;
using namespace pf::persist;

static PersistentState make_state() {
  PersistentState s;
  s.format_version = PF_PERSISTENCE_VERSION;
  s.coordinator_epoch = CoordinatorEpoch(1);
  s.policy_generation = PolicyGeneration(1);
  s.workload_id = WorkloadId(10);
  s.workload_generation = WorkloadGeneration(1);
  s.execution_id = ExecutionId(20);
  s.execution_generation = ExecutionGeneration(1);
  s.attempt_id = AttemptId(30);
  s.attempt_generation = AttemptGeneration(1);
  s.worker_id = WorkerId(40);
  s.worker_boot_id = WorkerBootId(50);
  s.fenced_worker_boot = WorkerBootId(50);
  s.lifecycle = LifecycleState::kPreempted;
  s.request_id = PreemptionRequestId(60);
  s.request_generation = PreemptionRequestGeneration(1);
  s.quiesced = true;
  s.quiescence_generation = QuiescenceGeneration(1);
  s.state_durable = true;
  s.state_capture_generation = StateCaptureGeneration(1);
  s.checkpoint_ref = CheckpointRef(70);
  s.checkpoint_generation = CheckpointGeneration(1);
  s.resources_released = true;
  s.release_generation = ReleaseGeneration(1);
  s.preempted = true;
  s.resume_generation = ResumeGeneration(0);
  s.durable_progress = 500;
  s.progress_generation = ProgressGeneration(1);
  PersistentSafePoint sp;
  sp.id = SafePointId(80);
  sp.generation = SafePointGeneration(1);
  sp.execution_id = ExecutionId(20);
  sp.attempt_id = AttemptId(30);
  sp.worker_id = WorkerId(40);
  sp.worker_boot_id = WorkerBootId(50);
  sp.progress_position = 500;
  sp.state_capture_required = true;
  sp.checkpoint_required = false;
  sp.state = SafePointState::kValid;
  sp.order = 1;
  s.safe_points.push_back(sp);
  PersistentReleaseReceipt rcpt;
  rcpt.resource = ResourceClass::kDeviceMemory;
  rcpt.units = 100;
  rcpt.contract_id = ResourceContractId(90);
  rcpt.contract_generation = ResourceContractGeneration(1);
  rcpt.release_generation = ReleaseGeneration(1);
  rcpt.worker_boot_id = WorkerBootId(50);
  s.release_receipts.push_back(rcpt);
  return s;
}

int main() {
  TEST_CONTEXT("persistence");

  // Round-trip.
  auto bytes = encode(make_state());
  auto res = decode(bytes);
  CHECK(res.ok);
  CHECK(res.error == PersistenceError::kOk);
  if (res.ok) {
    CHECK_EQ(res.state.execution_generation.value(), 1);
    CHECK_EQ(res.state.durable_progress, 500);
    CHECK(res.state.lifecycle == LifecycleState::kPreempted);
    CHECK_EQ(res.state.safe_points.size(), 1);
    CHECK_EQ(res.state.release_receipts.size(), 1);
  }

  // Truncation.
  auto trunc = std::vector<std::byte>(bytes.begin(), bytes.begin() + bytes.size() - 3);
  auto r2 = decode(trunc);
  CHECK(!r2.ok);
  CHECK(r2.error == PersistenceError::kTruncated);

  // Bad magic.
  auto badmagic = bytes;
  badmagic[0] = static_cast<std::byte>('X');
  auto r3 = decode(badmagic);
  CHECK(!r3.ok);
  CHECK(r3.error == PersistenceError::kBadMagic);

  // Trailing garbage.
  auto trail = bytes;
  trail.push_back(static_cast<std::byte>(0xAB));
  auto r4 = decode(trail);
  CHECK(!r4.ok);
  CHECK(r4.error == PersistenceError::kTruncated || r4.error == PersistenceError::kChecksum);

  // Corrupt a payload byte -> checksum failure.
  auto corr = bytes;
  corr[40] = static_cast<std::byte>(std::to_integer<unsigned char>(corr[40]) ^ 0xFF);
  auto r5 = decode(corr);
  CHECK(!r5.ok);

  // Illegal lifecycle: release without quiescence.
  auto bad = make_state();
  bad.quiesced = false;
  auto badbytes = encode(bad);
  auto r6 = decode(badbytes);
  CHECK(!r6.ok);
  CHECK(r6.error == PersistenceError::kReleaseBeforeQuiescence);

  // Resume before preempted.
  auto bad2 = make_state();
  bad2.lifecycle = LifecycleState::kResumePending;
  bad2.preempted = false;
  auto bad2bytes = encode(bad2);
  auto r7 = decode(bad2bytes);
  CHECK(!r7.ok);
  CHECK(r7.error == PersistenceError::kResumeBeforePreempted);

  // Broken checkpoint reference.
  auto bad3 = make_state();
  bad3.state_durable = true;
  bad3.checkpoint_ref = CheckpointRef(0);
  auto bad3bytes = encode(bad3);
  auto r8 = decode(bad3bytes);
  CHECK(!r8.ok);
  CHECK(r8.error == PersistenceError::kBrokenCheckpoint);

  // Bad version header.
  auto bv = bytes;
  bv[5] = static_cast<std::byte>(0xFF);  // version byte
  auto r9 = decode(bv);
  CHECK(!r9.ok);

  PF_TEST_RETURN();
}
