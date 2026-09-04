#include "tests/test_framework.hpp"
#include "preemption_fabric/lifecycle/lifecycle.hpp"
#include "preemption_fabric/util/error.hpp"

using namespace pf;

int main() {
  TEST_CONTEXT("lifecycle");

  // Drive a legal sequence through to PREEMPTED/RESUMED, asserting each edge.
  LifecycleStateMachine s;
  s.reset(LifecycleState::kRequested);
  CHECK(s.try_transition(LifecycleState::kValidatingAuthority));
  CHECK(s.try_transition(LifecycleState::kAssessing));
  CHECK(s.try_transition(LifecycleState::kWaitingForSafePoint));
  CHECK(s.try_transition(LifecycleState::kSafePointReached));
  CHECK(s.try_transition(LifecycleState::kQuiescing));
  CHECK(s.try_transition(LifecycleState::kQuiesced));
  CHECK(s.try_transition(LifecycleState::kCaptureRequired));
  CHECK(s.try_transition(LifecycleState::kCapturingState));
  CHECK(s.try_transition(LifecycleState::kStateDurable));
  CHECK(s.try_transition(LifecycleState::kReleasePending));
  CHECK(s.try_transition(LifecycleState::kReleasing));
  CHECK(s.try_transition(LifecycleState::kResourcesReleased));
  CHECK(s.try_transition(LifecycleState::kPreempted));
  CHECK(s.try_transition(LifecycleState::kResumePending));
  CHECK(s.try_transition(LifecycleState::kRevalidating));
  CHECK(s.try_transition(LifecycleState::kRestoring));
  CHECK(s.try_transition(LifecycleState::kResumeReady));
  CHECK(s.try_transition(LifecycleState::kResuming));
  CHECK(s.try_transition(LifecycleState::kResumed));
  CHECK(s.is_terminal());
  CHECK(s.is_preempted_durable());

  // Illegal transitions are rejected deterministically.
  CHECK(LifecycleStateMachine::is_allowed(LifecycleState::kRequested, LifecycleState::kResourcesReleased) == false);
  CHECK(LifecycleStateMachine::is_allowed(LifecycleState::kPreempted, LifecycleState::kResumed) == false);
  CHECK(LifecycleStateMachine::is_allowed(LifecycleState::kQuiesced, LifecycleState::kPreempted) == false);
  CHECK(LifecycleStateMachine::is_allowed(LifecycleState::kResourcesReleased, LifecycleState::kStateDurable) == false);

  // Resumed is terminal.
  LifecycleStateMachine t;
  t.reset(LifecycleState::kResumed);
  CHECK(!t.try_transition(LifecycleState::kPreempted));

  // Throwing transition message is specific.
  LifecycleStateMachine bad;
  bad.reset(LifecycleState::kRequested);
  bool threw = false;
  try { bad.transition(LifecycleState::kResourcesReleased); } catch (const PreemptionError& e) { threw = true; CHECK_EQ(std::string(e.code()), "illegal_transition"); }
  CHECK(threw);
  CHECK(bad.state() == LifecycleState::kRequested);

  PF_TEST_RETURN();
}
