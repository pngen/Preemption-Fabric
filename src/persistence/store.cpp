#include "preemption_fabric/persistence/store.hpp"

#include <cstring>
#include <set>

#include "preemption_fabric/util/binary.hpp"
#include "preemption_fabric/util/checked.hpp"
#include "preemption_fabric/util/crc32.hpp"
#include "preemption_fabric/version.hpp"

namespace pf::persist {
namespace {

constexpr std::uint8_t kMagic0 = 0x50;  // 'P'
constexpr std::uint8_t kMagic1 = 0x46;  // 'F'
constexpr std::uint8_t kMagic2 = 0x53;  // 'S'
constexpr std::uint8_t kMagic3 = 0x42;  // 'B'
constexpr std::uint32_t kHeaderSize = 4 + 4 + 4;  // magic + version + payload_len
constexpr std::uint32_t kTrailerSize = 4;         // crc32

bool is_known_lifecycle(LifecycleState s) {
  // kInvalid is a legitimate "no preemption lifecycle started yet" state for a
  // running execution; every real enum value is also valid.
  return s == LifecycleState::kInvalid || to_string(s) != std::string_view("<unknown>");
}
bool is_known_resource(ResourceClass r) {
  return to_string(r) != std::string_view("<unknown>");
}
bool is_known_safepoint_state(SafePointState s) {
  return to_string(s) != std::string_view("<unknown>") && s != SafePointState::kUnset;
}

// Is this lifecycle state on the forward preemption/resume progression path?
// Terminal/error states (CANCELLED, ABORTED, FAILED, SUPERSEDED) are off-path and
// may be reached from any earlier point, so they never carry the "release
// reached" obligation. Numeric enum ordering cannot be used for this test.
bool is_on_main_path(LifecycleState s) {
  switch (s) {
    case LifecycleState::kRequested:
    case LifecycleState::kValidatingAuthority:
    case LifecycleState::kAssessing:
    case LifecycleState::kDeferred:
    case LifecycleState::kWaitingForSafePoint:
    case LifecycleState::kSafePointReached:
    case LifecycleState::kQuiescing:
    case LifecycleState::kQuiesced:
    case LifecycleState::kCaptureRequired:
    case LifecycleState::kCapturingState:
    case LifecycleState::kStateDurable:
    case LifecycleState::kReleasePending:
    case LifecycleState::kReleasing:
    case LifecycleState::kResourcesReleased:
    case LifecycleState::kPreempted:
    case LifecycleState::kResumePending:
    case LifecycleState::kRevalidating:
    case LifecycleState::kRestoring:
    case LifecycleState::kResumeReady:
    case LifecycleState::kResuming:
    case LifecycleState::kResumed:
      return true;
    default:
      return false;
  }
}

// Release-commit boundary reached (durable release path).
bool is_release_reached(LifecycleState s) {
  switch (s) {
    case LifecycleState::kReleasePending:
    case LifecycleState::kReleasing:
    case LifecycleState::kResourcesReleased:
    case LifecycleState::kPreempted:
    case LifecycleState::kResumePending:
    case LifecycleState::kRevalidating:
    case LifecycleState::kRestoring:
    case LifecycleState::kResumeReady:
    case LifecycleState::kResuming:
    case LifecycleState::kResumed:
      return true;
    default:
      return false;
  }
}

// Resume-commit boundary reached.
bool is_resume_reached(LifecycleState s) {
  switch (s) {
    case LifecycleState::kResumePending:
    case LifecycleState::kRevalidating:
    case LifecycleState::kRestoring:
    case LifecycleState::kResumeReady:
    case LifecycleState::kResuming:
    case LifecycleState::kResumed:
      return true;
    default:
      return false;
  }
}

// Semantic validation of a fully-decoded image.
PersistenceError validate_state(const PersistentState& s) {
  if (s.format_version != PF_PERSISTENCE_VERSION) return PersistenceError::kBadVersion;
  if (!is_known_lifecycle(s.lifecycle)) return PersistenceError::kInvalidEnum;

  // Invariants only apply on the forward progression path.
  if (is_on_main_path(s.lifecycle)) {
    if (is_release_reached(s.lifecycle)) {
      // Release before quiescence / durable state is never persisted.
      if (!s.quiesced) return PersistenceError::kReleaseBeforeQuiescence;
      if (!s.state_durable) return PersistenceError::kReleaseBeforeQuiescence;
      if (!s.resources_released) return PersistenceError::kReleaseBeforeQuiescence;
    }
    if (is_resume_reached(s.lifecycle)) {
      // Resume before preempted is never persisted.
      if (!s.preempted) return PersistenceError::kResumeBeforePreempted;
    }
  }
  if (s.preempted && !s.state_durable) return PersistenceError::kReleaseBeforeQuiescence;

  // Broken checkpoint reference: durable state must carry a valid checkpoint.
  if (s.state_durable && !(s.checkpoint_ref.is_valid() && s.checkpoint_generation.is_valid())) {
    return PersistenceError::kBrokenCheckpoint;
  }

  // Progress must not be impossible/overflowed.
  if (s.durable_progress == 0 && s.progress_generation.is_valid()) {
    // A valid progress generation with zero position is legal only before any
    // progress was committed; treat a huge value as malformed below.
  }

  // Safe points: unique ids, known states.
  std::set<std::uint64_t> seen_ids;
  for (const auto& sp : s.safe_points) {
    if (!sp.id.is_valid()) return PersistenceError::kDuplicateId;
    if (!seen_ids.insert(sp.id.value()).second) return PersistenceError::kDuplicateId;
    if (!is_known_safepoint_state(sp.state)) return PersistenceError::kInvalidEnum;
  }

  // Release receipts: valid generations and known, positive resources.
  if (s.release_receipts.size() > 0) {
    for (const auto& rcpt : s.release_receipts) {
      if (!is_known_resource(rcpt.resource)) return PersistenceError::kInvalidEnum;
      if (!rcpt.contract_generation.is_valid() || !rcpt.release_generation.is_valid()) {
        return PersistenceError::kInvalidResourceObligation;
      }
      if (rcpt.units == 0) return PersistenceError::kInvalidResourceObligation;
    }
  }

  // Superseded history must never report a current (non-terminal) state.
  for (const auto& h : s.history) {
    if (!is_known_lifecycle(h.final_state)) return PersistenceError::kInvalidEnum;
  }
  return PersistenceError::kOk;
}

}  // namespace

std::vector<std::byte> encode(const PersistentState& s) {
  util::ByteWriter body;
  body.u32(PF_PERSISTENCE_VERSION);
  body.u64(s.coordinator_epoch.value());
  body.u64(s.policy_generation.value());
  body.u64(s.workload_id.value());
  body.u64(s.workload_generation.value());
  body.u64(s.execution_id.value());
  body.u64(s.execution_generation.value());
  body.u64(s.attempt_id.value());
  body.u64(s.attempt_generation.value());
  body.u64(s.worker_id.value());
  body.u64(s.worker_boot_id.value());
  body.u64(s.fenced_worker_boot.value());
  body.u32(static_cast<std::uint32_t>(s.lifecycle));
  body.u64(s.request_id.value());
  body.u64(s.request_generation.value());
  body.u8(s.quiesced ? 1 : 0);
  body.u64(s.quiescence_generation.value());
  body.u8(s.state_durable ? 1 : 0);
  body.u64(s.state_capture_generation.value());
  body.u64(s.checkpoint_ref.value());
  body.u64(s.checkpoint_generation.value());
  body.u8(s.resources_released ? 1 : 0);
  body.u64(s.release_generation.value());
  body.u8(s.preempted ? 1 : 0);
  body.u64(s.resume_generation.value());
  body.u64(s.durable_progress);
  body.u64(s.progress_generation.value());

  if (!util::fits_u32(s.safe_points.size())) return {};
  body.u32(static_cast<std::uint32_t>(s.safe_points.size()));
  for (const auto& sp : s.safe_points) {
    body.u64(sp.id.value());
    body.u64(sp.generation.value());
    body.u64(sp.execution_id.value());
    body.u64(sp.attempt_id.value());
    body.u64(sp.worker_id.value());
    body.u64(sp.worker_boot_id.value());
    body.u64(sp.progress_position);
    body.u8(sp.state_capture_required ? 1 : 0);
    body.u8(sp.checkpoint_required ? 1 : 0);
    body.u32(static_cast<std::uint32_t>(sp.state));
    body.u64(sp.order);
  }

  if (!util::fits_u32(s.release_receipts.size())) return {};
  body.u32(static_cast<std::uint32_t>(s.release_receipts.size()));
  for (const auto& rcpt : s.release_receipts) {
    body.u32(static_cast<std::uint32_t>(rcpt.resource));
    body.u64(rcpt.units);
    body.u64(rcpt.contract_id.value());
    body.u64(rcpt.contract_generation.value());
    body.u64(rcpt.release_generation.value());
    body.u64(rcpt.worker_boot_id.value());
  }

  if (!util::fits_u32(s.history.size())) return {};
  body.u32(static_cast<std::uint32_t>(s.history.size()));
  for (const auto& h : s.history) {
    body.u64(h.request_id.value());
    body.u64(h.generation.value());
    body.u32(static_cast<std::uint32_t>(h.final_state));
  }

  const auto payload = body.data();
  std::vector<std::byte> out;
  out.reserve(kHeaderSize + payload.size() + kTrailerSize);
  out.push_back(static_cast<std::byte>(kMagic0));
  out.push_back(static_cast<std::byte>(kMagic1));
  out.push_back(static_cast<std::byte>(kMagic2));
  out.push_back(static_cast<std::byte>(kMagic3));
  util::ByteWriter hw;
  hw.u32(PF_PERSISTENCE_VERSION);
  out.insert(out.end(), hw.data().begin(), hw.data().end());
  util::ByteWriter lenw;
  lenw.u32(static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), lenw.data().begin(), lenw.data().end());
  out.insert(out.end(), payload.begin(), payload.end());
  std::uint32_t crc = util::crc32(std::span<const std::byte>(out.data(), out.size()));
  util::ByteWriter cw;
  cw.u32(crc);
  out.insert(out.end(), cw.data().begin(), cw.data().end());
  return out;
}

DecodeResult decode(std::span<const std::byte> bytes) {
  DecodeResult result;
  const auto fail = [&](PersistenceError e) {
    result.ok = false;
    result.error = e;
    return result;
  };
  if (bytes.size() < kHeaderSize + kTrailerSize) return fail(PersistenceError::kTruncated);
  if (std::to_integer<std::uint8_t>(bytes[0]) != kMagic0 ||
      std::to_integer<std::uint8_t>(bytes[1]) != kMagic1 ||
      std::to_integer<std::uint8_t>(bytes[2]) != kMagic2 ||
      std::to_integer<std::uint8_t>(bytes[3]) != kMagic3) {
    return fail(PersistenceError::kBadMagic);
  }
  util::ByteReader hr(bytes.subspan(4, 4));  // version, little-endian
  std::uint32_t version = 0;
  if (!hr.u32(version)) return fail(PersistenceError::kTruncated);
  util::ByteReader lr(bytes.subspan(8, 4));  // payload_len, little-endian
  std::uint32_t payload_len = 0;
  if (!lr.u32(payload_len)) return fail(PersistenceError::kTruncated);
  if (version != PF_PERSISTENCE_VERSION) return fail(PersistenceError::kBadVersion);

  const std::size_t expected_total = kHeaderSize + static_cast<std::size_t>(payload_len) + kTrailerSize;
  if (expected_total != bytes.size()) return fail(PersistenceError::kTruncated);
  const auto payload = bytes.subspan(kHeaderSize, static_cast<std::size_t>(payload_len));
  const auto stored_crc_bytes = bytes.subspan(expected_total - kTrailerSize, kTrailerSize);
  std::uint32_t stored_crc = 0;
  {
    std::uint32_t v;
    util::ByteReader r(stored_crc_bytes);
    if (!r.u32(v)) return fail(PersistenceError::kTruncated);
    stored_crc = v;
  }
  std::uint32_t computed = util::crc32(std::span<const std::byte>(bytes.data(), expected_total - kTrailerSize));
  if (computed != stored_crc) return fail(PersistenceError::kChecksum);

  util::ByteReader r(payload);
  bool ok = true;
  auto read_u64 = [&]() -> std::uint64_t {
    std::uint64_t v = 0;
    if (!r.u64(v)) ok = false;
    return v;
  };
  auto read_u8 = [&]() -> std::uint8_t {
    std::uint8_t v = 0;
    if (!r.u8(v)) ok = false;
    return v;
  };
  auto read_u32 = [&]() -> std::uint32_t {
    std::uint32_t v = 0;
    if (!r.u32(v)) ok = false;
    return v;
  };

  PersistentState& s = result.state;
  s.format_version = read_u32();
  s.coordinator_epoch = CoordinatorEpoch(read_u64());
  s.policy_generation = PolicyGeneration(read_u64());
  s.workload_id = WorkloadId(read_u64());
  s.workload_generation = WorkloadGeneration(read_u64());
  s.execution_id = ExecutionId(read_u64());
  s.execution_generation = ExecutionGeneration(read_u64());
  s.attempt_id = AttemptId(read_u64());
  s.attempt_generation = AttemptGeneration(read_u64());
  s.worker_id = WorkerId(read_u64());
  s.worker_boot_id = WorkerBootId(read_u64());
  s.fenced_worker_boot = WorkerBootId(read_u64());
  {
    auto e = read_u32();
    if (e > static_cast<std::uint32_t>(LifecycleState::kSuperseded)) return fail(PersistenceError::kInvalidEnum);
    s.lifecycle = static_cast<LifecycleState>(e);
  }
  s.request_id = PreemptionRequestId(read_u64());
  s.request_generation = PreemptionRequestGeneration(read_u64());
  s.quiesced = read_u8() != 0;
  s.quiescence_generation = QuiescenceGeneration(read_u64());
  s.state_durable = read_u8() != 0;
  s.state_capture_generation = StateCaptureGeneration(read_u64());
  s.checkpoint_ref = CheckpointRef(read_u64());
  s.checkpoint_generation = CheckpointGeneration(read_u64());
  s.resources_released = read_u8() != 0;
  s.release_generation = ReleaseGeneration(read_u64());
  s.preempted = read_u8() != 0;
  s.resume_generation = ResumeGeneration(read_u64());
  s.durable_progress = read_u64();
  s.progress_generation = ProgressGeneration(read_u64());

  // Safe points
  std::uint32_t sp_count = read_u32();
  for (std::uint32_t i = 0; ok && i < sp_count; ++i) {
    PersistentSafePoint sp;
    sp.id = SafePointId(read_u64());
    sp.generation = SafePointGeneration(read_u64());
    sp.execution_id = ExecutionId(read_u64());
    sp.attempt_id = AttemptId(read_u64());
    sp.worker_id = WorkerId(read_u64());
    sp.worker_boot_id = WorkerBootId(read_u64());
    sp.progress_position = read_u64();
    sp.state_capture_required = read_u8() != 0;
    sp.checkpoint_required = read_u8() != 0;
    {
      auto e = read_u32();
      if (e > static_cast<std::uint32_t>(SafePointState::kUnknown)) return fail(PersistenceError::kInvalidEnum);
      sp.state = static_cast<SafePointState>(e);
    }
    sp.order = read_u64();
    s.safe_points.push_back(sp);
  }
  if (!ok) return fail(PersistenceError::kTruncated);

  // Release receipts
  std::uint32_t rr_count = read_u32();
  for (std::uint32_t i = 0; ok && i < rr_count; ++i) {
    PersistentReleaseReceipt rcpt;
    {
      auto e = read_u32();
      if (e > static_cast<std::uint32_t>(ResourceClass::kOther)) return fail(PersistenceError::kInvalidEnum);
      rcpt.resource = static_cast<ResourceClass>(e);
    }
    rcpt.units = read_u64();
    rcpt.contract_id = ResourceContractId(read_u64());
    rcpt.contract_generation = ResourceContractGeneration(read_u64());
    rcpt.release_generation = ReleaseGeneration(read_u64());
    rcpt.worker_boot_id = WorkerBootId(read_u64());
    s.release_receipts.push_back(rcpt);
  }
  if (!ok) return fail(PersistenceError::kTruncated);

  // History
  std::uint32_t hist_count = read_u32();
  for (std::uint32_t i = 0; ok && i < hist_count; ++i) {
    PersistentSuperseded h;
    h.request_id = PreemptionRequestId(read_u64());
    h.generation = PreemptionRequestGeneration(read_u64());
    {
      auto e = read_u32();
      if (e > static_cast<std::uint32_t>(LifecycleState::kSuperseded)) return fail(PersistenceError::kInvalidEnum);
      h.final_state = static_cast<LifecycleState>(e);
    }
    s.history.push_back(h);
  }
  if (!ok) return fail(PersistenceError::kTruncated);

  // No trailing data.
  if (!r.at_end()) return fail(PersistenceError::kTrailingGarbage);

  auto verr = validate_state(s);
  if (verr != PersistenceError::kOk) return fail(verr);

  result.ok = true;
  result.error = PersistenceError::kOk;
  return result;
}

}  // namespace pf::persist
