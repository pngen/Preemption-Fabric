#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "preemption_fabric/core/enum_codec.hpp"

// All strongly typed enums used by Preemption Fabric. Every enum has a matching
// constexpr name table so explanations, persistence, and the protocol can use
// stable names rather than vague strings. UNKNOWN is a first-class value that
// never silently upgrades to a concrete, safe outcome.

namespace pf {

// ---------------------------------------------------------------------------
// Why a preemption was requested.
// ---------------------------------------------------------------------------
enum class PreemptionReason : std::uint32_t {
  kInvalid = 0,
  kHigherPriorityWork = 1,
  kResourceReclamation = 2,
  kFairness = 3,
  kSloProtection = 4,
  kAdministrative = 5,
  kMigration = 6,
  kCapacityPressure = 7,
  kThermalOrPowerPressure = 8,
  kMaintenance = 9,
  kDrain = 10,
  kPolicyChange = 11,
  kDependencyChange = 12,
  kUnknown = 13
};
inline constexpr auto kPreemptionReasonNames = std::to_array<std::pair<PreemptionReason, std::string_view>>({
  PF_ENUM_ENTRY(PreemptionReason, kHigherPriorityWork, "HIGHER_PRIORITY_WORK"),
  PF_ENUM_ENTRY(PreemptionReason, kResourceReclamation, "RESOURCE_RECLAMATION"),
  PF_ENUM_ENTRY(PreemptionReason, kFairness, "FAIRNESS"),
  PF_ENUM_ENTRY(PreemptionReason, kSloProtection, "SLO_PROTECTION"),
  PF_ENUM_ENTRY(PreemptionReason, kAdministrative, "ADMINISTRATIVE"),
  PF_ENUM_ENTRY(PreemptionReason, kMigration, "MIGRATION"),
  PF_ENUM_ENTRY(PreemptionReason, kCapacityPressure, "CAPACITY_PRESSURE"),
  PF_ENUM_ENTRY(PreemptionReason, kThermalOrPowerPressure, "THERMAL_OR_POWER_PRESSURE"),
  PF_ENUM_ENTRY(PreemptionReason, kMaintenance, "MAINTENANCE"),
  PF_ENUM_ENTRY(PreemptionReason, kDrain, "DRAIN"),
  PF_ENUM_ENTRY(PreemptionReason, kPolicyChange, "POLICY_CHANGE"),
  PF_ENUM_ENTRY(PreemptionReason, kDependencyChange, "DEPENDENCY_CHANGE"),
  PF_ENUM_ENTRY(PreemptionReason, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(PreemptionReason v) noexcept {
  return pf::detail::name_of(v, kPreemptionReasonNames);
}
[[nodiscard]] inline constexpr std::optional<PreemptionReason> preemption_reason_from_string(std::string_view s) noexcept {
  return pf::detail::value_of<PreemptionReason>(s, kPreemptionReasonNames);
}

// ---------------------------------------------------------------------------
// Is this work safely interruptible right now, and at what boundary?
// ---------------------------------------------------------------------------
enum class PreemptibilityClass : std::uint32_t {
  kInvalid = 0,
  kPreemptibleNow = 1,
  kPreemptibleAtSafePoint = 2,
  kPreemptibleAfterStateCapture = 3,
  kPreemptibleAfterCommitBoundary = 4,
  kRecomputableFromDurableProgress = 5,
  kTemporarilyUnsafe = 6,
  kNonPreemptible = 7,
  kUnsupported = 8,
  kUnknown = 9
};
inline constexpr auto kPreemptibilityClassNames = std::to_array<std::pair<PreemptibilityClass, std::string_view>>({
  PF_ENUM_ENTRY(PreemptibilityClass, kPreemptibleNow, "PREEMPTIBLE_NOW"),
  PF_ENUM_ENTRY(PreemptibilityClass, kPreemptibleAtSafePoint, "PREEMPTIBLE_AT_SAFE_POINT"),
  PF_ENUM_ENTRY(PreemptibilityClass, kPreemptibleAfterStateCapture, "PREEMPTIBLE_AFTER_STATE_CAPTURE"),
  PF_ENUM_ENTRY(PreemptibilityClass, kPreemptibleAfterCommitBoundary, "PREEMPTIBLE_AFTER_COMMIT_BOUNDARY"),
  PF_ENUM_ENTRY(PreemptibilityClass, kRecomputableFromDurableProgress, "RECOMPUTABLE_FROM_DURABLE_PROGRESS"),
  PF_ENUM_ENTRY(PreemptibilityClass, kTemporarilyUnsafe, "TEMPORARILY_UNSAFE"),
  PF_ENUM_ENTRY(PreemptibilityClass, kNonPreemptible, "NON_PREEMPTIBLE"),
  PF_ENUM_ENTRY(PreemptibilityClass, kUnsupported, "UNSUPPORTED"),
  PF_ENUM_ENTRY(PreemptibilityClass, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(PreemptibilityClass v) noexcept {
  return pf::detail::name_of(v, kPreemptibilityClassNames);
}

// ---------------------------------------------------------------------------
// The distinct outcome of a preemption attempt. Forced OS process termination
// is never the same thing as successful safe preemption.
// ---------------------------------------------------------------------------
enum class PreemptionOutcome : std::uint32_t {
  kInvalid = 0,
  kSafePreemptionCompleted = 1,
  kDeferToSafePoint = 2,
  kStateCaptureRequired = 3,
  kCannotPreemptSafely = 4,
  kNonPreemptible = 5,
  kForceTerminationRequired = 6,
  kPreemptionAborted = 7,
  kPreemptionFailed = 8
};
inline constexpr auto kPreemptionOutcomeNames = std::to_array<std::pair<PreemptionOutcome, std::string_view>>({
  PF_ENUM_ENTRY(PreemptionOutcome, kSafePreemptionCompleted, "SAFE_PREEMPTION_COMPLETED"),
  PF_ENUM_ENTRY(PreemptionOutcome, kDeferToSafePoint, "DEFER_TO_SAFE_POINT"),
  PF_ENUM_ENTRY(PreemptionOutcome, kStateCaptureRequired, "STATE_CAPTURE_REQUIRED"),
  PF_ENUM_ENTRY(PreemptionOutcome, kCannotPreemptSafely, "CANNOT_PREEMPT_SAFELY"),
  PF_ENUM_ENTRY(PreemptionOutcome, kNonPreemptible, "NON_PREEMPTIBLE"),
  PF_ENUM_ENTRY(PreemptionOutcome, kForceTerminationRequired, "FORCE_TERMINATION_REQUIRED"),
  PF_ENUM_ENTRY(PreemptionOutcome, kPreemptionAborted, "PREEMPTION_ABORTED"),
  PF_ENUM_ENTRY(PreemptionOutcome, kPreemptionFailed, "PREEMPTION_FAILED")
});
[[nodiscard]] inline constexpr std::string_view to_string(PreemptionOutcome v) noexcept {
  return pf::detail::name_of(v, kPreemptionOutcomeNames);
}

// ---------------------------------------------------------------------------
// The guarded preemption lifecycle.
// ---------------------------------------------------------------------------
enum class LifecycleState : std::uint32_t {
  kInvalid = 0,
  kRequested = 1,
  kValidatingAuthority = 2,
  kAssessing = 3,
  kDeferred = 4,
  kWaitingForSafePoint = 5,
  kSafePointReached = 6,
  kQuiescing = 7,
  kQuiesced = 8,
  kCaptureRequired = 9,
  kCapturingState = 10,
  kStateDurable = 11,
  kReleasePending = 12,
  kReleasing = 13,
  kResourcesReleased = 14,
  kPreempted = 15,
  kResumePending = 16,
  kRevalidating = 17,
  kRestoring = 18,
  kResumeReady = 19,
  kResuming = 20,
  kResumed = 21,
  kCancelled = 22,
  kAborted = 23,
  kFailed = 24,
  kSuperseded = 25
};
inline constexpr auto kLifecycleStateNames = std::to_array<std::pair<LifecycleState, std::string_view>>({
  PF_ENUM_ENTRY(LifecycleState, kRequested, "REQUESTED"),
  PF_ENUM_ENTRY(LifecycleState, kValidatingAuthority, "VALIDATING_AUTHORITY"),
  PF_ENUM_ENTRY(LifecycleState, kAssessing, "ASSESSING"),
  PF_ENUM_ENTRY(LifecycleState, kDeferred, "DEFERRED"),
  PF_ENUM_ENTRY(LifecycleState, kWaitingForSafePoint, "WAITING_FOR_SAFE_POINT"),
  PF_ENUM_ENTRY(LifecycleState, kSafePointReached, "SAFE_POINT_REACHED"),
  PF_ENUM_ENTRY(LifecycleState, kQuiescing, "QUIESCING"),
  PF_ENUM_ENTRY(LifecycleState, kQuiesced, "QUIESCED"),
  PF_ENUM_ENTRY(LifecycleState, kCaptureRequired, "CAPTURE_REQUIRED"),
  PF_ENUM_ENTRY(LifecycleState, kCapturingState, "CAPTURING_STATE"),
  PF_ENUM_ENTRY(LifecycleState, kStateDurable, "STATE_DURABLE"),
  PF_ENUM_ENTRY(LifecycleState, kReleasePending, "RELEASE_PENDING"),
  PF_ENUM_ENTRY(LifecycleState, kReleasing, "RELEASING"),
  PF_ENUM_ENTRY(LifecycleState, kResourcesReleased, "RESOURCES_RELEASED"),
  PF_ENUM_ENTRY(LifecycleState, kPreempted, "PREEMPTED"),
  PF_ENUM_ENTRY(LifecycleState, kResumePending, "RESUME_PENDING"),
  PF_ENUM_ENTRY(LifecycleState, kRevalidating, "REVALIDATING"),
  PF_ENUM_ENTRY(LifecycleState, kRestoring, "RESTORING"),
  PF_ENUM_ENTRY(LifecycleState, kResumeReady, "RESUME_READY"),
  PF_ENUM_ENTRY(LifecycleState, kResuming, "RESUMING"),
  PF_ENUM_ENTRY(LifecycleState, kResumed, "RESUMED"),
  PF_ENUM_ENTRY(LifecycleState, kCancelled, "CANCELLED"),
  PF_ENUM_ENTRY(LifecycleState, kAborted, "ABORTED"),
  PF_ENUM_ENTRY(LifecycleState, kFailed, "FAILED"),
  PF_ENUM_ENTRY(LifecycleState, kSuperseded, "SUPERSEDED")
});
[[nodiscard]] inline constexpr std::string_view to_string(LifecycleState v) noexcept {
  return pf::detail::name_of(v, kLifecycleStateNames);
}
[[nodiscard]] inline constexpr std::optional<LifecycleState> lifecycle_state_from_string(std::string_view s) noexcept {
  return pf::detail::value_of<LifecycleState>(s, kLifecycleStateNames);
}

// ---------------------------------------------------------------------------
// State of a first-class safe point.
// ---------------------------------------------------------------------------
enum class SafePointState : std::uint32_t {
  kUnset = 0,
  kDeclared = 1,
  kReached = 2,
  kValidating = 3,
  kValid = 4,
  kInvalid = 5,
  kSuperseded = 6,
  kStale = 7,
  kUnknown = 8
};
inline constexpr auto kSafePointStateNames = std::to_array<std::pair<SafePointState, std::string_view>>({
  PF_ENUM_ENTRY(SafePointState, kDeclared, "DECLARED"),
  PF_ENUM_ENTRY(SafePointState, kReached, "REACHED"),
  PF_ENUM_ENTRY(SafePointState, kValidating, "VALIDATING"),
  PF_ENUM_ENTRY(SafePointState, kValid, "VALID"),
  PF_ENUM_ENTRY(SafePointState, kInvalid, "INVALID"),
  PF_ENUM_ENTRY(SafePointState, kSuperseded, "SUPERSEDED"),
  PF_ENUM_ENTRY(SafePointState, kStale, "STALE"),
  PF_ENUM_ENTRY(SafePointState, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(SafePointState v) noexcept {
  return pf::detail::name_of(v, kSafePointStateNames);
}

// ---------------------------------------------------------------------------
// State-preservation outcome. Distinguishes "already durable" from "captured
// and verified" from "nothing durable", etc.
// ---------------------------------------------------------------------------
enum class StatePreservationOutcome : std::uint32_t {
  kInvalid = 0,
  kAlreadyDurable = 1,
  kCaptureRequired = 2,
  kCaptureInProgress = 3,
  kCapturedAndVerified = 4,
  kRecomputable = 5,
  kNoDurableState = 6,
  kCaptureFailed = 7,
  kStaleCapture = 8,
  kUnknown = 9
};
inline constexpr auto kStatePreservationOutcomeNames = std::to_array<std::pair<StatePreservationOutcome, std::string_view>>({
  PF_ENUM_ENTRY(StatePreservationOutcome, kAlreadyDurable, "ALREADY_DURABLE"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kCaptureRequired, "CAPTURE_REQUIRED"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kCaptureInProgress, "CAPTURE_IN_PROGRESS"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kCapturedAndVerified, "CAPTURED_AND_VERIFIED"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kRecomputable, "RECOMPUTABLE"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kNoDurableState, "NO_DURABLE_STATE"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kCaptureFailed, "CAPTURE_FAILED"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kStaleCapture, "STALE_CAPTURE"),
  PF_ENUM_ENTRY(StatePreservationOutcome, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(StatePreservationOutcome v) noexcept {
  return pf::detail::name_of(v, kStatePreservationOutcomeNames);
}

// ---------------------------------------------------------------------------
// Resume eligibility outcome. UNKNOWN never becomes RESUME_ELIGIBLE.
// ---------------------------------------------------------------------------
enum class ResumeOutcome : std::uint32_t {
  kInvalid = 0,
  kResumeEligible = 1,
  kResumeBlockedStaleAuthority = 2,
  kResumeBlockedStateMissing = 3,
  kResumeBlockedStateStale = 4,
  kResumeBlockedDependency = 5,
  kResumeBlockedResource = 6,
  kResumeBlockedCompatibility = 7,
  kResumeBlockedPolicy = 8,
  kResumeRevalidationRequired = 9,
  kResumeRestoreRequired = 10,
  kResumeRecomputeRequired = 11,
  kResumeNotAllowed = 12,
  kUnknown = 13
};
inline constexpr auto kResumeOutcomeNames = std::to_array<std::pair<ResumeOutcome, std::string_view>>({
  PF_ENUM_ENTRY(ResumeOutcome, kResumeEligible, "RESUME_ELIGIBLE"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedStaleAuthority, "RESUME_BLOCKED_STALE_AUTHORITY"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedStateMissing, "RESUME_BLOCKED_STATE_MISSING"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedStateStale, "RESUME_BLOCKED_STATE_STALE"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedDependency, "RESUME_BLOCKED_DEPENDENCY"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedResource, "RESUME_BLOCKED_RESOURCE"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedCompatibility, "RESUME_BLOCKED_COMPATIBILITY"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeBlockedPolicy, "RESUME_BLOCKED_POLICY"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeRevalidationRequired, "RESUME_REVALIDATION_REQUIRED"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeRestoreRequired, "RESUME_RESTORE_REQUIRED"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeRecomputeRequired, "RESUME_RECOMPUTE_REQUIRED"),
  PF_ENUM_ENTRY(ResumeOutcome, kResumeNotAllowed, "RESUME_NOT_ALLOWED"),
  PF_ENUM_ENTRY(ResumeOutcome, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(ResumeOutcome v) noexcept {
  return pf::detail::name_of(v, kResumeOutcomeNames);
}

// ---------------------------------------------------------------------------
// Measurement kind for cost/timing evidence. Never present estimated as measured.
// ---------------------------------------------------------------------------
enum class MeasurementKind : std::uint32_t {
  kInvalid = 0,
  kMeasured = 1,
  kDerived = 2,
  kEstimated = 3,
  kSynthetic = 4,
  kUnknown = 5
};
inline constexpr auto kMeasurementKindNames = std::to_array<std::pair<MeasurementKind, std::string_view>>({
  PF_ENUM_ENTRY(MeasurementKind, kMeasured, "MEASURED"),
  PF_ENUM_ENTRY(MeasurementKind, kDerived, "DERIVED"),
  PF_ENUM_ENTRY(MeasurementKind, kEstimated, "ESTIMATED"),
  PF_ENUM_ENTRY(MeasurementKind, kSynthetic, "SYNTHETIC"),
  PF_ENUM_ENTRY(MeasurementKind, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(MeasurementKind v) noexcept {
  return pf::detail::name_of(v, kMeasurementKindNames);
}

// ---------------------------------------------------------------------------
// Evidence provenance / data kind.
// ---------------------------------------------------------------------------
enum class EvidenceKind : std::uint32_t {
  kInvalid = 0,
  kReal = 1,
  kSynthetic = 2,
  kDerived = 3,
  kMeasured = 4,
  kEstimated = 5,
  kUnsupported = 6,
  kUnknown = 7
};
inline constexpr auto kEvidenceKindNames = std::to_array<std::pair<EvidenceKind, std::string_view>>({
  PF_ENUM_ENTRY(EvidenceKind, kReal, "REAL"),
  PF_ENUM_ENTRY(EvidenceKind, kSynthetic, "SYNTHETIC"),
  PF_ENUM_ENTRY(EvidenceKind, kDerived, "DERIVED"),
  PF_ENUM_ENTRY(EvidenceKind, kMeasured, "MEASURED"),
  PF_ENUM_ENTRY(EvidenceKind, kEstimated, "ESTIMATED"),
  PF_ENUM_ENTRY(EvidenceKind, kUnsupported, "UNSUPPORTED"),
  PF_ENUM_ENTRY(EvidenceKind, kUnknown, "UNKNOWN")
});
[[nodiscard]] inline constexpr std::string_view to_string(EvidenceKind v) noexcept {
  return pf::detail::name_of(v, kEvidenceKindNames);
}

// ---------------------------------------------------------------------------
// Resource classes the runtime may need to release or keep warm.
// ---------------------------------------------------------------------------
enum class ResourceClass : std::uint32_t {
  kInvalid = 0,
  kAcceleratorExecutionSlot = 1,
  kDeviceMemory = 2,
  kPinnedHostMemory = 3,
  kHostMemory = 4,
  kTemporaryBuffers = 5,
  kModelAdapterResidency = 6,
  kTransferReservation = 7,
  kNetworkBandwidth = 8,
  kStorageLease = 9,
  kRuntimeHandle = 10,
  kOther = 11
};
inline constexpr auto kResourceClassNames = std::to_array<std::pair<ResourceClass, std::string_view>>({
  PF_ENUM_ENTRY(ResourceClass, kAcceleratorExecutionSlot, "ACCELERATOR_EXECUTION_SLOT"),
  PF_ENUM_ENTRY(ResourceClass, kDeviceMemory, "DEVICE_MEMORY"),
  PF_ENUM_ENTRY(ResourceClass, kPinnedHostMemory, "PINNED_HOST_MEMORY"),
  PF_ENUM_ENTRY(ResourceClass, kHostMemory, "HOST_MEMORY"),
  PF_ENUM_ENTRY(ResourceClass, kTemporaryBuffers, "TEMPORARY_BUFFERS"),
  PF_ENUM_ENTRY(ResourceClass, kModelAdapterResidency, "MODEL_ADAPTER_RESIDENCY"),
  PF_ENUM_ENTRY(ResourceClass, kTransferReservation, "TRANSFER_RESERVATION"),
  PF_ENUM_ENTRY(ResourceClass, kNetworkBandwidth, "NETWORK_BANDWIDTH"),
  PF_ENUM_ENTRY(ResourceClass, kStorageLease, "STORAGE_LEASE"),
  PF_ENUM_ENTRY(ResourceClass, kRuntimeHandle, "RUNTIME_HANDLE"),
  PF_ENUM_ENTRY(ResourceClass, kOther, "OTHER")
});
[[nodiscard]] inline constexpr std::string_view to_string(ResourceClass v) noexcept {
  return pf::detail::name_of(v, kResourceClassNames);
}

// ---------------------------------------------------------------------------
// Partial-progress ordering.
// ---------------------------------------------------------------------------
enum class ProgressKind : std::uint32_t {
  kInvalid = 0,
  kAttempted = 1,
  kSpeculative = 2,
  kAcknowledged = 3,
  kCommittedLogical = 4,
  kDurable = 5,
  kRecomputable = 6,
  kDiscarded = 7,
  kLost = 8
};
inline constexpr auto kProgressKindNames = std::to_array<std::pair<ProgressKind, std::string_view>>({
  PF_ENUM_ENTRY(ProgressKind, kAttempted, "ATTEMPTED"),
  PF_ENUM_ENTRY(ProgressKind, kSpeculative, "SPECULATIVE"),
  PF_ENUM_ENTRY(ProgressKind, kAcknowledged, "ACKNOWLEDGED"),
  PF_ENUM_ENTRY(ProgressKind, kCommittedLogical, "COMMITTED_LOGICAL"),
  PF_ENUM_ENTRY(ProgressKind, kDurable, "DURABLE"),
  PF_ENUM_ENTRY(ProgressKind, kRecomputable, "RECOMPUTABLE"),
  PF_ENUM_ENTRY(ProgressKind, kDiscarded, "DISCARDED"),
  PF_ENUM_ENTRY(ProgressKind, kLost, "LOST")
});
[[nodiscard]] inline constexpr std::string_view to_string(ProgressKind v) noexcept {
  return pf::detail::name_of(v, kProgressKindNames);
}

// ---------------------------------------------------------------------------
// Quiescence categories the runtime asks owning runtimes about.
// ---------------------------------------------------------------------------
enum class QuiescenceCategory : std::uint32_t {
  kInvalid = 0,
  kCpuWorkerActivity = 1,
  kAcceleratorKernels = 2,
  kAsynchronousCopies = 3,
  kQueuedDeviceOperations = 4,
  kHostCallbacks = 5,
  kStorageWrites = 6,
  kNetworkActivity = 7,
  kStatePublication = 8,
  kCompletionPublication = 9,
  kPendingResourceMutation = 10
};
inline constexpr auto kQuiescenceCategoryNames = std::to_array<std::pair<QuiescenceCategory, std::string_view>>({
  PF_ENUM_ENTRY(QuiescenceCategory, kCpuWorkerActivity, "CPU_WORKER_ACTIVITY"),
  PF_ENUM_ENTRY(QuiescenceCategory, kAcceleratorKernels, "ACCELERATOR_KERNELS"),
  PF_ENUM_ENTRY(QuiescenceCategory, kAsynchronousCopies, "ASYNCHRONOUS_COPIES"),
  PF_ENUM_ENTRY(QuiescenceCategory, kQueuedDeviceOperations, "QUEUED_DEVICE_OPERATIONS"),
  PF_ENUM_ENTRY(QuiescenceCategory, kHostCallbacks, "HOST_CALLBACKS"),
  PF_ENUM_ENTRY(QuiescenceCategory, kStorageWrites, "STORAGE_WRITES"),
  PF_ENUM_ENTRY(QuiescenceCategory, kNetworkActivity, "NETWORK_ACTIVITY"),
  PF_ENUM_ENTRY(QuiescenceCategory, kStatePublication, "STATE_PUBLICATION"),
  PF_ENUM_ENTRY(QuiescenceCategory, kCompletionPublication, "COMPLETION_PUBLICATION"),
  PF_ENUM_ENTRY(QuiescenceCategory, kPendingResourceMutation, "PENDING_RESOURCE_MUTATION")
});
[[nodiscard]] inline constexpr std::string_view to_string(QuiescenceCategory v) noexcept {
  return pf::detail::name_of(v, kQuiescenceCategoryNames);
}
}
