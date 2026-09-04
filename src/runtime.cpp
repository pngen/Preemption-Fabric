#include "preemption_fabric/runtime.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "preemption_fabric/persistence/store.hpp"
#include "preemption_fabric/util/binary.hpp"
#include "preemption_fabric/util/error.hpp"
#include "preemption_fabric/version.hpp"

namespace pf {
namespace {

[[nodiscard]] bool known_preemptibility(PreemptibilityClass c) {
  return c != PreemptibilityClass::kInvalid && c != PreemptibilityClass::kUnknown;
}
[[nodiscard]] bool is_concretely_preemptible(PreemptibilityClass c) {
  return c == PreemptibilityClass::kPreemptibleNow || c == PreemptibilityClass::kPreemptibleAtSafePoint ||
         c == PreemptibilityClass::kPreemptibleAfterStateCapture ||
         c == PreemptibilityClass::kPreemptibleAfterCommitBoundary ||
         c == PreemptibilityClass::kRecomputableFromDurableProgress;
}
[[nodiscard]] bool is_unknown_preemptibility(PreemptibilityClass c) {
  return c == PreemptibilityClass::kUnknown || c == PreemptibilityClass::kInvalid;
}

}  // namespace

PreemptionRuntime::PreemptionRuntime(CoordinatorEpoch epoch, PolicyGeneration policy,
                                     adapter::ReferenceAdapters& adapters)
    : epoch_(epoch), policy_generation_(policy), adapters_(adapters) {}

void PreemptionRuntime::start_execution(WorkloadId w, WorkloadGeneration wg, ExecutionId e, ExecutionGeneration eg,
                                        AttemptId a, AttemptGeneration ag, WorkerId worker, WorkerBootId boot) {
  std::unique_lock lock(mutex_);
  workload_id_ = w;
  workload_generation_ = wg;
  execution_id_ = e;
  execution_generation_ = eg;
  attempt_id_ = a;
  attempt_generation_ = ag;
  worker_id_ = worker;
  worker_boot_ = boot;
  machine_.reset(LifecycleState::kInvalid);
  active_request_.reset();
  execution_generation_ = eg;
  adapters_.set_authoritative(e, a, eg, ag, epoch_, boot);
}

SafePointId PreemptionRuntime::declare_safe_point(const SafePoint& sp) {
  std::unique_lock lock(mutex_);
  SafePoint copy = sp;
  copy.state = SafePointState::kDeclared;
  safe_points_[copy.id] = copy;
  return copy.id;
}

void PreemptionRuntime::reach_safe_point(SafePointId id, std::uint64_t position, bool capture_required) {
  std::unique_lock lock(mutex_);
  auto it = safe_points_.find(id);
  if (it == safe_points_.end()) throw PreemptionError("unknown_safe_point", "unknown safe point");
  it->second.state = SafePointState::kValid;
  it->second.progress_position = position;
  it->second.state_capture_required = capture_required;
  current_safe_point_ = it->second;
  current_position_ = std::max(current_position_, position);
  if (machine_.state() == LifecycleState::kWaitingForSafePoint) {
    machine_.transition(LifecycleState::kSafePointReached);
  }
}

bool PreemptionRuntime::publish_progress(std::uint64_t position, ProgressKind kind) {
  std::unique_lock lock(mutex_);
  auto auth = adapters_.current_attempt_authority(execution_id_, attempt_id_);
  if (!auth.authoritative || auth.worker_boot_id != worker_boot_) return false;
  if (position < durable_position_) return false;  // progress never moves backward in-generation
  current_position_ = std::max(current_position_, position);
  progress_generation_ = progress_generation_.next();
  if (kind == ProgressKind::kDurable) {
    if (position >= durable_position_) {
      durable_position_ = position;
      durable_progress_generation_ = progress_generation_;
    }
  }
  return true;
}

void PreemptionRuntime::validate_attempt(const ExecutionId& exec, const AttemptId& attempt) const {
  if (exec != execution_id_ || attempt != attempt_id_) {
    throw PreemptionError("wrong_scope", "attempt does not belong to this execution scope");
  }
}

bool PreemptionRuntime::validate_authority(const PreemptionRequest& req) const {
  if (req.coordinator_epoch != epoch_) return false;
  if (req.execution_id != execution_id_ || req.attempt_id != attempt_id_) return false;
  if (req.execution_generation != execution_generation_) return false;
  if (req.attempt_generation != attempt_generation_) return false;
  // A request from a worker must carry the current worker boot id. A
  // coordinator-originated request may carry a default (unknown) boot id.
  if (req.worker_boot_id.is_valid() && req.worker_boot_id != worker_boot_) return false;
  return true;
}

bool PreemptionRuntime::request_matches_current(const PreemptionRequest& req) const {
  if (!active_request_) return false;
  return active_request_->id == req.id && active_request_->generation == req.generation;
}

RequestAdmission PreemptionRuntime::request_preemption(const PreemptionRequest& req) {
  std::unique_lock lock(mutex_);
  RequestAdmission out;
  if (!req.id.is_valid() || !req.generation.is_valid()) {
    out.code = RequestAdmissionCode::kRejectedInvalid;
    return out;
  }
  if (!validate_authority(req)) {
    out.code = RequestAdmissionCode::kRejectedStale;
    return out;
  }
  if (active_request_) {
    if (request_matches_current(req)) {
      out.code = RequestAdmissionCode::kIdempotentDuplicate;
      out.request_id = req.id;
      out.generation = req.generation;
      return out;
    }
    out.code = RequestAdmissionCode::kConflictingDuplicate;
    return out;
  }
  // First valid authoritative request. Drive the lifecycle through the
  // authority-validation and assessment phases so the request is observable
  // and rejections remain deterministic.
  active_request_ = req;
  authoritative_request_generation_ = req.generation;
  machine_.reset(LifecycleState::kRequested);
  machine_.transition(LifecycleState::kValidatingAuthority);
  machine_.transition(LifecycleState::kAssessing);
  out.code = RequestAdmissionCode::kAccepted;
  out.request_id = req.id;
  out.generation = req.generation;
  return out;
}

PreemptionAssessment PreemptionRuntime::assess() const {
  std::shared_lock lock(mutex_);
  PreemptionAssessment as;
  PreemptionPlan plan;
  plan.execution_id = execution_id_;
  plan.attempt_id = attempt_id_;
  plan.workload_id = workload_id_;
  plan.workload_generation = workload_generation_;
  plan.last_durable_progress = durable_position_;
  plan.current_position = current_position_;
  plan.max_rollback = current_position_ > durable_position_ ? current_position_ - durable_position_ : 0;

  if (current_safe_point_ && current_safe_point_->state == SafePointState::kValid) {
    if (current_safe_point_->state_capture_required) {
      as.preemptibility = PreemptibilityClass::kPreemptibleAfterStateCapture;
      plan.preservation_mode = PreservationMode::kCapture;
      plan.state_to_preserve_bytes = 1024;
    } else {
      as.preemptibility = PreemptibilityClass::kPreemptibleNow;
      plan.preservation_mode = PreservationMode::kAlreadyDurable;
    }
    plan.next_safe_point = current_safe_point_->id;
    plan.cost_to_next_safe_point = 0;
  } else if (!safe_points_.empty()) {
    // A safe point is declared/pending: we can only preempt at that boundary.
    as.preemptibility = PreemptibilityClass::kPreemptibleAtSafePoint;
    plan.preservation_mode = PreservationMode::kCapture;
    plan.next_safe_point = safe_points_.begin()->first;
    plan.cost_to_next_safe_point = 1;
    as.why.push_back({"DEFER_TO_SAFE_POINT", "work is between safe point boundaries", EvidenceKind::kDerived});
  } else if (machine_.state() == LifecycleState::kQuiesced || machine_.state() == LifecycleState::kStateDurable ||
             machine_.state() == LifecycleState::kReleasePending) {
    as.preemptibility = PreemptibilityClass::kPreemptibleNow;
    plan.preservation_mode = PreservationMode::kCapture;
  } else {
    // Nothing proves a safe, durable boundary exists. Never claim preemptible.
    as.preemptibility = PreemptibilityClass::kUnknown;
    plan.preservation_mode = PreservationMode::kUnknown;
    as.why.push_back({"UNKNOWN", "no valid safe point or durable boundary evidence", EvidenceKind::kDerived});
  }

  // Honest resource-release estimate.
  auto contract = adapters_.current_contract(execution_id_);
  for (const auto& lease : contract.leases) plan.resources_to_release.push_back(lease.resource);
  if (!plan.resources_to_release.empty()) plan.release_before_preempted = true;

  as.plan = plan;
  return as;
}

PreemptionResult PreemptionRuntime::begin_quiesce() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  const auto s = machine_.state();
  if (s == LifecycleState::kSafePointReached) {
    machine_.transition(LifecycleState::kQuiescing);
  } else if (s == LifecycleState::kAssessing || s == LifecycleState::kDeferred) {
    // Quiescence is legal only at a valid boundary. If we are not at one yet,
    // defer to the next safe point; otherwise start quiescing now.
    if (current_safe_point_ && current_safe_point_->state == SafePointState::kValid) {
      machine_.transition(LifecycleState::kQuiescing);
    } else {
      machine_.transition(LifecycleState::kWaitingForSafePoint);
      res.outcome = PreemptionOutcome::kDeferToSafePoint;
      res.lifecycle = machine_.state();
      res.evidence.push_back({"defer_to_safe_point", "work is between safe point boundaries", EvidenceKind::kDerived});
      return res;
    }
  } else if (s == LifecycleState::kWaitingForSafePoint) {
    res.outcome = PreemptionOutcome::kDeferToSafePoint;
    res.lifecycle = machine_.state();
    res.evidence.push_back({"defer_to_safe_point", "waiting for the next valid safe point", EvidenceKind::kDerived});
    return res;
  } else if (s == LifecycleState::kQuiescing) {
    res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
    res.lifecycle = machine_.state();
    return res;
  } else {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.lifecycle = machine_.state();
    res.evidence.push_back({"cannot_preempt", "no safe boundary reached", EvidenceKind::kDerived});
    return res;
  }
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

bool PreemptionRuntime::is_quiesced_all() const {
  for (auto cat : adapters_.categories()) {
    if (!adapters_.is_quiesced(cat)) return false;
  }
  return true;
}

PreemptionResult PreemptionRuntime::report_quiesced(const QuiescenceReport& report) {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  // Quiescence evidence must come from the current worker incarnation.
  if (report.worker_boot_id != worker_boot_) {
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"stale_quiescence", "quiescence evidence from stale worker boot", EvidenceKind::kDerived});
    return res;
  }
  if (machine_.state() != LifecycleState::kQuiescing) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_quiescing", "quiescence reported outside QUIESCING", EvidenceKind::kDerived});
    return res;
  }
  if (!report.fully_quiesced || !is_quiesced_all()) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_quiesced", "required activity is not quiesced", EvidenceKind::kDerived});
    return res;
  }
  machine_.transition(LifecycleState::kQuiesced);
  quiesced_ = true;
  quiescence_generation_ = report.generation;
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::request_state_capture() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kQuiesced) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_quiesced", "state capture requires quiescence", EvidenceKind::kDerived});
    return res;
  }
  const bool capture_required = current_safe_point_ && current_safe_point_->state_capture_required;
  if (!capture_required) {
    // No capture needed: already durable or recomputable. Go straight to release.
    machine_.transition(LifecycleState::kReleasePending);
    state_durable_ = true;
    res.outcome = PreemptionOutcome::kStateCaptureRequired;
    res.lifecycle = machine_.state();
    return res;
  }
  machine_.transition(LifecycleState::kCaptureRequired);
  machine_.transition(LifecycleState::kCapturingState);
  adapter::StateCaptureRequest req;
  req.required = true;
  req.expected_generation = checkpoint_generation_.next();
  req.capture_generation = StateCaptureGeneration(req.expected_generation.value());
  req.execution_generation = execution_generation_;
  req.attempt_generation = attempt_generation_;
  auto result = adapters_.request_capture(req);
  res.outcome = PreemptionOutcome::kStateCaptureRequired;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::report_state_captured(const adapter::CaptureResult& result) {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kCapturingState) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_capturing", "state capture reported outside CAPTURING_STATE", EvidenceKind::kDerived});
    return res;
  }
  if (!result.success || !result.completed) {
    // Capture failure must never produce STATE_DURABLE.
    machine_.transition(LifecycleState::kFailed);
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.lifecycle = machine_.state();
    res.evidence.push_back({"capture_failed", "state capture did not complete", EvidenceKind::kReal});
    return res;
  }
  if (!adapters_.validate_checkpoint(result.checkpoint_ref, result.checkpoint_generation)) {
    machine_.transition(LifecycleState::kFailed);
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.lifecycle = machine_.state();
    res.evidence.push_back({"stale_checkpoint", "capture generation is not authoritative", EvidenceKind::kReal});
    return res;
  }
  machine_.transition(LifecycleState::kStateDurable);
  state_durable_ = true;
  state_capture_generation_ = result.capture_generation;
  checkpoint_ref_ = result.checkpoint_ref;
  checkpoint_generation_ = result.checkpoint_generation;
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::request_release() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kStateDurable && machine_.state() != LifecycleState::kReleasePending) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"release_requires_durable_state", "release requires durable state", EvidenceKind::kDerived});
    return res;
  }
  if (!state_durable_) {
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"not_durable", "cannot release before durable state", EvidenceKind::kDerived});
    return res;
  }
  if (machine_.state() == LifecycleState::kStateDurable) {
    machine_.transition(LifecycleState::kReleasePending);
  }
  machine_.transition(LifecycleState::kReleasing);
  // Ask the broker to release each lease in the current contract.
  auto contract = adapters_.current_contract(execution_id_);
  for (const auto& lease : contract.leases) {
    adapters_.request_release(execution_id_, lease.resource, lease.units, contract.generation);
  }
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::report_released() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kReleasing) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_releasing", "release report outside RELEASING", EvidenceKind::kDerived});
    return res;
  }
  auto contract = adapters_.current_contract(execution_id_);
  for (const auto& lease : contract.leases) {
    if (adapters_.reserved(lease.resource) != 0) {
      res.outcome = PreemptionOutcome::kPreemptionFailed;
      res.lifecycle = machine_.state();
      res.evidence.push_back({"resources_not_released", "release obligations not fully satisfied",
                             EvidenceKind::kDerived});
      return res;
    }
  }
  machine_.transition(LifecycleState::kResourcesReleased);
  resources_released_ = true;
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::commit_preempted() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kResourcesReleased) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_released", "PREEMPTED requires resources released", EvidenceKind::kDerived});
    return res;
  }
  if (!quiesced_ || !state_durable_ || !resources_released_) {
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"missing_boundary", "quiescence/durability/release not all satisfied",
                           EvidenceKind::kDerived});
    return res;
  }
  machine_.transition(LifecycleState::kPreempted);
  fenced_worker_boot_ = worker_boot_;
  adapters_.fence_attempt(execution_id_, attempt_id_, execution_generation_);
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  res.evidence.push_back({"preempted", "execution stopped at a valid boundary with durable state and released resources",
                         EvidenceKind::kReal});
  return res;
}

ResumeEligibility PreemptionRuntime::request_resume() {
  std::unique_lock lock(mutex_);
  ResumeEligibility el;
  el.resume_generation = resume_generation_;
  if (machine_.state() != LifecycleState::kPreempted) {
    el.outcome = ResumeOutcome::kResumeNotAllowed;
    el.why.push_back({"not_preempted", "resume requires a durable PREEMPTED state", EvidenceKind::kDerived});
    return el;
  }
  // Fresh resume authority is mandatory.
  machine_.transition(LifecycleState::kResumePending);
  resume_generation_ = resume_generation_.next();
  el.resume_generation = resume_generation_;
  el.outcome = ResumeOutcome::kResumeRevalidationRequired;
  el.why.push_back({"revalidation_required", "resume eligibility must be recomputed under current authority",
                   EvidenceKind::kDerived});
  return el;
}

PreemptionResult PreemptionRuntime::revalidate() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kResumePending && machine_.state() != LifecycleState::kRevalidating) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_revalidating", "revalidation requires RESUME_PENDING", EvidenceKind::kDerived});
    return res;
  }
  if (machine_.state() == LifecycleState::kResumePending) machine_.transition(LifecycleState::kRevalidating);

  // Stale authority check: the preempted execution generation must still be the
  // one that was fenced, and the checkpoint generation must be current.
  if (checkpoint_ref_.is_valid() && !adapters_.validate_checkpoint(checkpoint_ref_, checkpoint_generation_)) {
    machine_.transition(LifecycleState::kFailed);
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"stale_checkpoint", "checkpoint generation is no longer current", EvidenceKind::kReal});
    return res;
  }
  auto dep = adapters_.resume_dependencies(execution_id_);
  if (dep.status == adapter::DependencyStatus::kStale || dep.status == adapter::DependencyStatus::kMissing) {
    // Resume is blocked, not failed: the durable preempted state remains intact.
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.lifecycle = machine_.state();
    res.evidence.push_back({"resume_blocked_dependency", "resume dependency is stale or missing",
                           EvidenceKind::kDerived});
    return res;
  }

  // Resources must be revalidated under the current contract.
  auto contract = adapters_.current_contract(execution_id_);
  (void)contract;

  machine_.transition(LifecycleState::kRestoring);
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::restore() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kRestoring) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_restoring", "restore requires RESTORING", EvidenceKind::kDerived});
    return res;
  }
  if (checkpoint_ref_.is_valid() && !adapters_.restore(checkpoint_ref_, checkpoint_generation_)) {
    machine_.transition(LifecycleState::kFailed);
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"restore_failed", "restore did not complete", EvidenceKind::kReal});
    return res;
  }
  machine_.transition(LifecycleState::kResumeReady);
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

PreemptionResult PreemptionRuntime::confirm_resumed() {
  std::unique_lock lock(mutex_);
  PreemptionResult res;
  res.lifecycle = machine_.state();
  if (machine_.state() != LifecycleState::kResumeReady && machine_.state() != LifecycleState::kResuming) {
    res.outcome = PreemptionOutcome::kCannotPreemptSafely;
    res.evidence.push_back({"not_resume_ready", "resume confirmation requires RESUME_READY", EvidenceKind::kDerived});
    return res;
  }
  // Grant fresh attempt authority before RESUMED.
  auto fresh = adapters_.grant_fresh_attempt_authority(execution_id_, attempt_id_, execution_generation_, worker_boot_);
  if (!fresh.authoritative) {
    res.outcome = PreemptionOutcome::kPreemptionFailed;
    res.evidence.push_back({"no_fresh_authority", "could not obtain fresh attempt authority", EvidenceKind::kDerived});
    return res;
  }
  if (machine_.state() == LifecycleState::kResumeReady) machine_.transition(LifecycleState::kResuming);
  machine_.transition(LifecycleState::kResumed);
  resumed_ = true;
  res.outcome = PreemptionOutcome::kSafePreemptionCompleted;
  res.lifecycle = machine_.state();
  return res;
}

void PreemptionRuntime::cancel() {
  std::unique_lock lock(mutex_);
  const auto s = machine_.state();
  if (s == LifecycleState::kResumed) return;  // terminal success
  if (machine_.is_terminal() && s != LifecycleState::kCancelled) return;
  // Cancellation precedence: always wins over a pending resume, and must win
  // over any stale resume message that might re-enter later.
  if (!machine_.is_terminal()) {
    machine_.reset(LifecycleState::kCancelled);
    if (active_request_) {
      persist::PersistentSuperseded hs;
      hs.request_id = active_request_->id;
      hs.generation = active_request_->generation;
      hs.final_state = LifecycleState::kCancelled;
      history_.push_back(hs);
    }
  }
}

void PreemptionRuntime::complete() {
  std::unique_lock lock(mutex_);
  // Exactly one terminal interpretation may survive: if already PREEMPTED (or
  // beyond) completion is rejected; if not yet preempted, completion wins.
  if (machine_.state() == LifecycleState::kResumed || machine_.state() == LifecycleState::kCancelled ||
      machine_.state() == LifecycleState::kResumePending || machine_.state() == LifecycleState::kRevalidating ||
      machine_.state() == LifecycleState::kRestoring || machine_.state() == LifecycleState::kResumeReady ||
      machine_.state() == LifecycleState::kResuming) {
    // Completion cannot compete with an in-progress or completed preemption.
    return;
  }
  if (!machine_.is_terminal()) {
    machine_.reset(LifecycleState::kAborted);
  }
}

void PreemptionRuntime::record_explain(std::vector<ExplainEntry>& out, std::string message, std::string detail,
                                       EvidenceKind kind) const {
  out.push_back({std::move(message), std::move(detail), kind});
}

std::vector<ExplainEntry> PreemptionRuntime::explain_why_not_preemptible() const {
  return assess().why;
}

std::vector<ExplainEntry> PreemptionRuntime::explain_resume_blockers() const {
  std::shared_lock lock(mutex_);
  std::vector<ExplainEntry> out;
  if (machine_.state() != LifecycleState::kPreempted && !machine_.is_preempted_durable()) {
    out.push_back({"resume_not_allowed", "execution is not in a durable PREEMPTED state", EvidenceKind::kDerived});
    return out;
  }
  if (!state_durable_) out.push_back({"resume_blocked_state", "no durable state boundary", EvidenceKind::kDerived});
  if (!resources_released_) out.push_back({"resume_blocked_resource", "resources not released", EvidenceKind::kDerived});
  if (!checkpoint_ref_.is_valid()) out.push_back({"resume_blocked_state_missing", "no checkpoint reference", EvidenceKind::kDerived});
  if (checkpoint_ref_.is_valid() && !adapters_.validate_checkpoint(checkpoint_ref_, checkpoint_generation_)) {
    out.push_back({"resume_blocked_state_stale", "checkpoint generation is stale", EvidenceKind::kReal});
  }
  auto dep = adapters_.resume_dependencies(execution_id_);
  if (dep.status == adapter::DependencyStatus::kStale)
    out.push_back({"resume_blocked_dependency", "dependency is stale", EvidenceKind::kDerived});
  else if (dep.status == adapter::DependencyStatus::kMissing)
    out.push_back({"resume_blocked_dependency", "dependency is missing", EvidenceKind::kDerived});
  if (out.empty()) out.push_back({"resume_eligible", "resume is eligible", EvidenceKind::kDerived});
  return out;
}

std::vector<ExplainEntry> PreemptionRuntime::explain_state() const {
  std::shared_lock lock(mutex_);
  std::vector<ExplainEntry> out;
  out.push_back({"lifecycle", std::string(to_string(machine_.state())), EvidenceKind::kDerived});
  out.push_back({"durable_progress", std::to_string(durable_position_), EvidenceKind::kDerived});
  out.push_back({"current_position", std::to_string(current_position_), EvidenceKind::kDerived});
  out.push_back({"quiesced", quiesced_ ? "true" : "false", EvidenceKind::kDerived});
  out.push_back({"state_durable", state_durable_ ? "true" : "false", EvidenceKind::kDerived});
  out.push_back({"resources_released", resources_released_ ? "true" : "false", EvidenceKind::kDerived});
  return out;
}

PriorityInversionEvidence PreemptionRuntime::priority_inversion_assessment(
    const PriorityInversionInput& input) const {
  std::shared_lock lock(mutex_);
  PriorityInversionEvidence ev;
  ev.resource = input.resource;
  ev.holder_workload = input.holder_workload;
  ev.blocked_workload = input.blocked_workload;
  ev.reason = input.reason;
  ev.provenance = input.provenance;

  if (input.resource == ResourceClass::kInvalid || !input.holder_workload.is_valid() ||
      !input.blocked_workload.is_valid()) {
    return ev;  // invalid -> ok=false, resolution Unknown
  }
  // A stale policy generation is rejected (invalid, not merely stale).
  if (input.policy_generation != policy_generation_) {
    ev.stale = false;
    return ev;
  }
  // A stale priority generation is rejected deterministically.
  if (!input.priority_generation.is_valid() || input.priority_generation != priority_generation_) {
    ev.stale = true;
    return ev;
  }

  ev.ok = true;
  ev.next_safe_point_cost = input.next_safe_point_cost;
  ev.preservation_cost = input.preservation_cost;
  ev.release_cost = input.release_cost;
  ev.resume_recompute_cost = input.resume_recompute_cost;
  ev.blocking_duration_ns = input.blocking_duration_ns;
  ev.blocking_duration_measured = input.blocking_duration_measured;

  ev.holder_preemptible = is_concretely_preemptible(input.holder_preemptibility);
  if (!input.holder_authoritative) {
    ev.resolution = PriorityInversionResolution::kSafePreemptionDoesNotResolve;
  } else if (is_unknown_preemptibility(input.holder_preemptibility)) {
    // UNKNOWN never becomes a positive recommendation.
    ev.resolution = PriorityInversionResolution::kUnknown;
  } else if (ev.holder_preemptible) {
    ev.resolution = PriorityInversionResolution::kSafePreemptionResolves;
  } else {
    ev.resolution = PriorityInversionResolution::kSafePreemptionDoesNotResolve;
  }
  return ev;
}

adapter::MigrationCompatibility PreemptionRuntime::evaluate_migration_destination(
    const adapter::DeviceCapability& dst) const {
  std::shared_lock lock(mutex_);
  adapter::MigrationCompatibility r;
  if (!machine_.is_preempted_durable() || !state_durable_) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kStateNotDurable;
    r.detail = "execution is not in a durable PREEMPTED state";
    r.reasons.push_back("state not durable");
    return r;
  }
  if (!checkpoint_ref_.is_valid() || !checkpoint_generation_.is_valid()) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kIncompatibleCapability;
    r.detail = "preserved state is not bound to a valid checkpoint generation";
    r.reasons.push_back("no checkpoint generation");
    return r;
  }
  if (!adapters_.capability_generation_current(dst)) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kStaleCapabilityGeneration;
    r.detail = "destination capability generation is stale";
    r.reasons.push_back("stale capability generation");
    return r;
  }
  if (!adapters_.capability_is_compatible(dst)) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kIncompatibleCapability;
    r.detail = "destination capability is incompatible with the preserved state";
    r.reasons.push_back("incompatible destination");
    return r;
  }
  auto dep = adapters_.resume_dependencies(execution_id_);
  if (dep.status == adapter::DependencyStatus::kStale || dep.status == adapter::DependencyStatus::kMissing) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kBlockedDependency;
    r.detail = "resume dependency is stale or missing";
    r.reasons.push_back("stale dependency");
    return r;
  }
  auto contract = adapters_.current_contract(execution_id_);
  if (!contract.generation.is_valid()) {
    r.outcome = adapter::MigrationCompatibilityOutcome::kBlockedResource;
    r.detail = "resource contract generation is not current";
    r.reasons.push_back("stale resource contract");
    return r;
  }
  r.outcome = adapter::MigrationCompatibilityOutcome::kCompatible;
  r.detail = "destination is compatible with the preserved state and current generations";
  r.reasons.push_back("compatible");
  return r;
}

void PreemptionRuntime::set_worker_incarnation(WorkerId worker, WorkerBootId boot) {
  std::unique_lock lock(mutex_);
  worker_id_ = worker;
  worker_boot_ = boot;
  // A fresh worker incarnation never inherits prior fence state.
  fenced_worker_boot_ = {};
}

LifecycleState PreemptionRuntime::lifecycle() const {
  std::shared_lock lock(mutex_);
  return machine_.state();
}

PreemptibilityClass PreemptionRuntime::current_preemptibility() const {
  return assess().preemptibility;
}

std::uint64_t PreemptionRuntime::durable_progress() const {
  std::shared_lock lock(mutex_);
  return durable_position_;
}

std::uint64_t PreemptionRuntime::current_position() const {
  std::shared_lock lock(mutex_);
  return current_position_;
}

PreemptionRequestGeneration PreemptionRuntime::authoritative_request_generation() const {
  std::shared_lock lock(mutex_);
  return authoritative_request_generation_;
}

bool PreemptionRuntime::is_attempt_authoritative() const {
  auto auth = adapters_.current_attempt_authority(execution_id_, attempt_id_);
  return auth.authoritative;
}

persist::PersistentState PreemptionRuntime::snapshot() const {
  std::shared_lock lock(mutex_);
  persist::PersistentState s;
  s.format_version = PF_PERSISTENCE_VERSION;
  s.coordinator_epoch = epoch_;
  s.policy_generation = policy_generation_;
  s.workload_id = workload_id_;
  s.workload_generation = workload_generation_;
  s.execution_id = execution_id_;
  s.execution_generation = execution_generation_;
  s.attempt_id = attempt_id_;
  s.attempt_generation = attempt_generation_;
  s.worker_id = worker_id_;
  s.worker_boot_id = worker_boot_;
  s.fenced_worker_boot = fenced_worker_boot_;
  s.lifecycle = machine_.state();
  if (active_request_) {
    s.request_id = active_request_->id;
    s.request_generation = active_request_->generation;
  }
  s.quiesced = quiesced_;
  s.quiescence_generation = quiescence_generation_;
  s.state_durable = state_durable_;
  s.state_capture_generation = state_capture_generation_;
  s.checkpoint_ref = checkpoint_ref_;
  s.checkpoint_generation = checkpoint_generation_;
  s.resources_released = resources_released_;
  s.release_generation = release_generation_;
  s.preempted = machine_.is_preempted_durable();
  s.resume_generation = resume_generation_;
  s.durable_progress = durable_position_;
  s.progress_generation = durable_progress_generation_;
  for (const auto& [id, sp] : safe_points_) {
    persist::PersistentSafePoint psp;
    psp.id = sp.id;
    psp.generation = sp.generation;
    psp.execution_id = sp.execution_id;
    psp.attempt_id = sp.attempt_id;
    psp.worker_id = sp.worker_id;
    psp.worker_boot_id = sp.worker_boot_id;
    psp.progress_position = sp.progress_position;
    psp.state_capture_required = sp.state_capture_required;
    psp.checkpoint_required = sp.checkpoint_required;
    psp.state = sp.state;
    psp.order = sp.order;
    s.safe_points.push_back(psp);
  }
  s.history = history_;
  return s;
}

void PreemptionRuntime::restore(const persist::PersistentState& st) {
  std::unique_lock lock(mutex_);
  workload_id_ = st.workload_id;
  workload_generation_ = st.workload_generation;
  execution_id_ = st.execution_id;
  execution_generation_ = st.execution_generation;
  attempt_id_ = st.attempt_id;
  attempt_generation_ = st.attempt_generation;
  worker_id_ = st.worker_id;
  worker_boot_ = st.worker_boot_id;
  fenced_worker_boot_ = st.fenced_worker_boot;
  machine_.reset(st.lifecycle);
  quiesced_ = st.quiesced;
  quiescence_generation_ = st.quiescence_generation;
  state_durable_ = st.state_durable;
  state_capture_generation_ = st.state_capture_generation;
  checkpoint_ref_ = st.checkpoint_ref;
  checkpoint_generation_ = st.checkpoint_generation;
  resources_released_ = st.resources_released;
  release_generation_ = st.release_generation;
  resume_generation_ = st.resume_generation;
  durable_position_ = st.durable_progress;
  durable_progress_generation_ = st.progress_generation;
  current_position_ = st.durable_progress;
  if (st.request_id.is_valid()) {
    PreemptionRequest req;
    req.id = st.request_id;
    req.generation = st.request_generation;
    req.workload_id = st.workload_id;
    req.workload_generation = st.workload_generation;
    req.execution_id = st.execution_id;
    req.execution_generation = st.execution_generation;
    req.attempt_id = st.attempt_id;
    req.attempt_generation = st.attempt_generation;
    req.coordinator_epoch = epoch_;
    active_request_ = req;
    authoritative_request_generation_ = st.request_generation;
  }
  safe_points_.clear();
  for (const auto& psp : st.safe_points) {
    SafePoint sp;
    sp.id = psp.id;
    sp.generation = psp.generation;
    sp.execution_id = psp.execution_id;
    sp.attempt_id = psp.attempt_id;
    sp.worker_id = psp.worker_id;
    sp.worker_boot_id = psp.worker_boot_id;
    sp.progress_position = psp.progress_position;
    sp.state_capture_required = psp.state_capture_required;
    sp.checkpoint_required = psp.checkpoint_required;
    sp.state = psp.state;
    sp.order = psp.order;
    safe_points_[psp.id] = sp;
  }
  history_ = st.history;
  adapters_.set_authoritative(execution_id_, attempt_id_, execution_generation_, attempt_generation_, epoch_, worker_boot_);
}

std::vector<std::byte> PreemptionRuntime::persist_bytes() const { return persist::encode(snapshot()); }

bool PreemptionRuntime::restore_bytes(std::span<const std::byte> bytes) {
  auto res = persist::decode(bytes);
  if (!res.ok) return false;
  restore(res.state);
  return true;
}

bool PreemptionRuntime::save(const std::string& path) const {
  auto bytes = persist_bytes();
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

bool PreemptionRuntime::load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return restore_bytes(bytes);
}

}  // namespace pf
