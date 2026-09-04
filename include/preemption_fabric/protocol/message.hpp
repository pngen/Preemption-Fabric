#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"
#include "preemption_fabric/adapters/interfaces.hpp"

namespace pf::proto {

// The reference coordinator/worker protocol. Every frame carries the authority
// context needed to reject stale messages deterministically: coordinator epoch,
// worker identity/boot id, execution/attempt generations, and request
// generation. An old WorkerBootId, a stale CoordinatorEpoch, or an old
// attempt/request generation can never be accepted as current.
enum class MessageType : std::uint32_t {
  kInvalid = 0,
  kHello = 1,
  kRegister = 2,
  kBeginExecution = 3,
  kPublishProgress = 4,
  kDeclareSafePoint = 5,
  kReachSafePoint = 6,
  kRequestPreemption = 7,
  kPreemptionAssessment = 8,
  kBeginQuiesce = 9,
  kQuiesced = 10,
  kRequestStateCapture = 11,
  kStateCaptured = 12,
  kRequestRelease = 13,
  kResourceReleased = 14,
  kPreempted = 15,
  kRequestResume = 16,
  kRevalidationResult = 17,
  kRestoreResult = 18,
  kResumed = 19,
  kCancel = 20,
  kComplete = 21,
  kError = 22,
  kShutdown = 23,
  kReplayStale = 24
};

inline constexpr std::string_view to_string(MessageType t) noexcept {
  switch (t) {
    case MessageType::kHello: return "HELLO";
    case MessageType::kRegister: return "REGISTER";
    case MessageType::kBeginExecution: return "BEGIN_EXECUTION";
    case MessageType::kPublishProgress: return "PUBLISH_PROGRESS";
    case MessageType::kDeclareSafePoint: return "DECLARE_SAFE_POINT";
    case MessageType::kReachSafePoint: return "REACH_SAFE_POINT";
    case MessageType::kRequestPreemption: return "REQUEST_PREEMPTION";
    case MessageType::kPreemptionAssessment: return "PREEMPTION_ASSESSMENT";
    case MessageType::kBeginQuiesce: return "BEGIN_QUIESCE";
    case MessageType::kQuiesced: return "QUIESCED";
    case MessageType::kRequestStateCapture: return "REQUEST_STATE_CAPTURE";
    case MessageType::kStateCaptured: return "STATE_CAPTURED";
    case MessageType::kRequestRelease: return "REQUEST_RELEASE";
    case MessageType::kResourceReleased: return "RESOURCE_RELEASED";
    case MessageType::kPreempted: return "PREEMPTED";
    case MessageType::kRequestResume: return "REQUEST_RESUME";
    case MessageType::kRevalidationResult: return "REVALIDATION_RESULT";
    case MessageType::kRestoreResult: return "RESTORE_RESULT";
    case MessageType::kResumed: return "RESUMED";
    case MessageType::kCancel: return "CANCEL";
    case MessageType::kComplete: return "COMPLETE";
    case MessageType::kError: return "ERROR";
    case MessageType::kShutdown: return "SHUTDOWN";
    case MessageType::kReplayStale: return "REPLAY_STALE";
  }
  return "INVALID";
}

// A single protocol message with a complete authority context. The flat layout
// keeps encode/decode strictly bounds-checked and deterministic; correctness and
// integrity are the primary concerns, not byte density.
struct Message {
  MessageType type = MessageType::kInvalid;
  std::uint32_t seq = 0;  // per-sender monotonic sequence (replay/dedup)

  CoordinatorEpoch coordinator_epoch;
  WorkerId worker_id;
  WorkerBootId worker_boot_id;

  ExecutionId execution_id;
  ExecutionGeneration execution_generation;
  AttemptId attempt_id;
  AttemptGeneration attempt_generation;

  PreemptionRequestId request_id;
  PreemptionRequestGeneration request_generation;

  // Second worker / target worker for registration & migration cases.
  WorkerId worker_b;

  std::uint64_t progress_position = 0;
  ProgressGeneration progress_generation;
  ProgressKind progress_kind = ProgressKind::kInvalid;

  SafePointId safe_point_id;
  SafePointGeneration safe_point_generation;
  std::uint64_t safe_point_position = 0;
  bool safe_point_capture_required = false;

  CheckpointRef checkpoint_ref;
  CheckpointGeneration checkpoint_generation;
  StateCaptureId capture_id;
  StateCaptureGeneration capture_generation;

  ResumeId resume_id;
  ResumeGeneration resume_generation;

  ResourceClass resource = ResourceClass::kInvalid;
  std::uint64_t units = 0;
  ResourceContractId contract_id;
  ResourceContractGeneration contract_generation;

  PreemptionReason reason = PreemptionReason::kUnknown;
  PreemptionOutcome outcome = PreemptionOutcome::kInvalid;
  LifecycleState lifecycle = LifecycleState::kInvalid;
  PreemptibilityClass preemptibility = PreemptibilityClass::kUnknown;
  ResumeOutcome resume_outcome = ResumeOutcome::kUnknown;
  adapter::DependencyStatus dep_status = adapter::DependencyStatus::kUnknown;
  StatePreservationOutcome preservation_outcome = StatePreservationOutcome::kUnknown;

  std::string detail;   // bounded human-readable / error text
  std::string extra;
};

}  // namespace pf::proto
