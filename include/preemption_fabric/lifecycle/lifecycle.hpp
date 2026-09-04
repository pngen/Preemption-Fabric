#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/util/error.hpp"

namespace pf {

// The guarded preemption lifecycle state machine.
//
// Every transition is explicit and deterministic. Illegal transitions are
// rejected (throwing PreemptionError) before any side effect occurs. In
// particular:
//   - REQUESTED -> RESOURCES_RELEASED without passing through quiescence is
//     impossible (no direct edge exists).
//   - PREEMPTED -> RESUMED without fresh authority is impossible (RESUMED is
//     reachable only through RESUME_PENDING -> REVALIDATING -> ... -> RESUMING).
//   - A stale worker, a stale checkpoint, or a superseded preemption can never
//     produce the state it claims (the runtime enforces generation fencing; the
//     state machine enforces the ordering).
class LifecycleStateMachine {
 public:
  LifecycleStateMachine() noexcept : state_(LifecycleState::kInvalid) {}
  explicit LifecycleStateMachine(LifecycleState initial) noexcept : state_(initial) {}

  [[nodiscard]] LifecycleState state() const noexcept { return state_; }

  // Whether an edge from 'from' to 'to' is allowed by the state machine.
  [[nodiscard]] static bool is_allowed(LifecycleState from, LifecycleState to) noexcept {
    return contains(frontier(from), to);
  }

  // Transition, throwing PreemptionError on any illegal edge.
  void transition(LifecycleState to) {
    if (!is_allowed(state_, to)) {
      throw_invalid_transition(to_string(state_).data(), to_string(to).data());
    }
    state_ = to;
  }

  // Non-throwing conditional transition; returns false and leaves state
  // unchanged on an illegal edge. Used to short-circuit internal transitions.
  [[nodiscard]] bool try_transition(LifecycleState to) noexcept {
    if (!is_allowed(state_, to)) return false;
    state_ = to;
    return true;
  }

  [[nodiscard]] bool is_terminal() const noexcept {
    return state_ == LifecycleState::kCancelled || state_ == LifecycleState::kAborted ||
           state_ == LifecycleState::kFailed || state_ == LifecycleState::kSuperseded ||
           state_ == LifecycleState::kResumed;
  }

  // Has this lifecycle reached (and committed) a durable preempted boundary?
  [[nodiscard]] bool is_preempted_durable() const noexcept {
    return state_ == LifecycleState::kPreempted || state_ == LifecycleState::kResumePending ||
           state_ == LifecycleState::kRevalidating || state_ == LifecycleState::kRestoring ||
           state_ == LifecycleState::kResumeReady || state_ == LifecycleState::kResuming ||
           state_ == LifecycleState::kResumed;
  }

  // States that themselves represent a durable commit boundary.
  [[nodiscard]] static bool is_durable_commit(LifecycleState s) noexcept {
    return s == LifecycleState::kStateDurable || s == LifecycleState::kResourcesReleased ||
           s == LifecycleState::kPreempted || s == LifecycleState::kResumed;
  }

  void reset(LifecycleState s) noexcept { state_ = s; }

 private:
  // The exclusive set of states reachable from each source. Kept small and
  // explicit; an unlisted edge is illegal by construction.
  [[nodiscard]] static std::span<const LifecycleState> frontier(LifecycleState s) noexcept {
    switch (s) {
      case LifecycleState::kInvalid: return {};  // no outgoing
      case LifecycleState::kRequested: {
        static constexpr LifecycleState f[] = {LifecycleState::kValidatingAuthority, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kValidatingAuthority: {
        static constexpr LifecycleState f[] = {LifecycleState::kAssessing, LifecycleState::kCancelled, LifecycleState::kSuperseded, LifecycleState::kFailed};
        return f;
      }
      case LifecycleState::kAssessing: {
        static constexpr LifecycleState f[] = {LifecycleState::kDeferred, LifecycleState::kWaitingForSafePoint, LifecycleState::kQuiescing, LifecycleState::kCaptureRequired, LifecycleState::kCancelled, LifecycleState::kSuperseded, LifecycleState::kFailed};
        return f;
      }
      case LifecycleState::kDeferred: {
        static constexpr LifecycleState f[] = {LifecycleState::kWaitingForSafePoint, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kWaitingForSafePoint: {
        static constexpr LifecycleState f[] = {LifecycleState::kSafePointReached, LifecycleState::kCancelled, LifecycleState::kSuperseded, LifecycleState::kFailed};
        return f;
      }
      case LifecycleState::kSafePointReached: {
        static constexpr LifecycleState f[] = {LifecycleState::kQuiescing, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kQuiescing: {
        static constexpr LifecycleState f[] = {LifecycleState::kQuiesced, LifecycleState::kCancelled, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kQuiesced: {
        static constexpr LifecycleState f[] = {LifecycleState::kCaptureRequired, LifecycleState::kReleasePending, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kCaptureRequired: {
        static constexpr LifecycleState f[] = {LifecycleState::kCapturingState, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kCapturingState: {
        static constexpr LifecycleState f[] = {LifecycleState::kStateDurable, LifecycleState::kFailed, LifecycleState::kSuperseded, LifecycleState::kCancelled};
        return f;
      }
      case LifecycleState::kStateDurable: {
        static constexpr LifecycleState f[] = {LifecycleState::kReleasePending, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kReleasePending: {
        static constexpr LifecycleState f[] = {LifecycleState::kReleasing, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kReleasing: {
        static constexpr LifecycleState f[] = {LifecycleState::kResourcesReleased, LifecycleState::kFailed, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kResourcesReleased: {
        static constexpr LifecycleState f[] = {LifecycleState::kPreempted, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kPreempted: {
        static constexpr LifecycleState f[] = {LifecycleState::kResumePending, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kResumePending: {
        static constexpr LifecycleState f[] = {LifecycleState::kRevalidating, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kRevalidating: {
        static constexpr LifecycleState f[] = {LifecycleState::kRestoring, LifecycleState::kResumeReady, LifecycleState::kCancelled, LifecycleState::kSuperseded, LifecycleState::kFailed};
        return f;
      }
      case LifecycleState::kRestoring: {
        static constexpr LifecycleState f[] = {LifecycleState::kResumeReady, LifecycleState::kFailed, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kResumeReady: {
        static constexpr LifecycleState f[] = {LifecycleState::kResuming, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      case LifecycleState::kResuming: {
        static constexpr LifecycleState f[] = {LifecycleState::kResumed, LifecycleState::kFailed, LifecycleState::kCancelled, LifecycleState::kSuperseded};
        return f;
      }
      // Terminal states have no outgoing edges.
      case LifecycleState::kResumed:
      case LifecycleState::kCancelled:
      case LifecycleState::kAborted:
      case LifecycleState::kFailed:
      case LifecycleState::kSuperseded:
        return {};
    }
    return {};
  }

  [[nodiscard]] static bool contains(std::span<const LifecycleState> f, LifecycleState s) noexcept {
    for (auto x : f) if (x == s) return true;
    return false;
  }

  LifecycleState state_ = LifecycleState::kInvalid;
};

}  // namespace pf
