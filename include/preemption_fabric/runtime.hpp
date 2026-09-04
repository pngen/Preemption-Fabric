#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "preemption_fabric/adapters/interfaces.hpp"
#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"
#include "preemption_fabric/domains/plan.hpp"
#include "preemption_fabric/domains/progress.hpp"
#include "preemption_fabric/domains/quiescence.hpp"
#include "preemption_fabric/domains/request.hpp"
#include "preemption_fabric/domains/resume.hpp"
#include "preemption_fabric/domains/safepoint.hpp"
#include "preemption_fabric/lifecycle/lifecycle.hpp"
#include "preemption_fabric/persistence/state.hpp"
#include "preemption_fabric/priority/inversion.hpp"

namespace pf {

// Admission result for a preemption request.
enum class RequestAdmissionCode : std::uint32_t {
  kInvalid = 0,
  kAccepted = 1,
  kIdempotentDuplicate = 2,
  kConflictingDuplicate = 3,
  kRejectedStale = 4,
  kRejectedInvalid = 5
};

struct RequestAdmission {
  RequestAdmissionCode code = RequestAdmissionCode::kInvalid;
  PreemptionRequestId request_id;
  PreemptionRequestGeneration generation;
};

// A structured explanation entry (typed reason + human-readable text).
struct ExplainEntry {
  std::string message;
  std::string detail;
  EvidenceKind provenance = EvidenceKind::kUnknown;
};

// A single preemption assessment: is this preemptible now, why, and what plan.
struct PreemptionAssessment {
  PreemptibilityClass preemptibility = PreemptibilityClass::kUnknown;
  PreemptionPlan plan;
  std::vector<ExplainEntry> why;
};

// Outcome of committing a preemption.
struct PreemptionResult {
  PreemptionOutcome outcome = PreemptionOutcome::kInvalid;
  LifecycleState lifecycle = LifecycleState::kInvalid;
  std::vector<ExplainEntry> evidence;
};

// Resume eligibility result.
struct ResumeEligibility {
  ResumeOutcome outcome = ResumeOutcome::kUnknown;
  ResumeGeneration resume_generation;
  std::vector<ExplainEntry> why;
};

// The production runtime for a single authoritative execution scope.
//
// Preemption Fabric governs only the safe interruption protocol itself: whether
// work is currently preemptible, where the next valid boundary is, how work
// reaches quiescence, what progress is durable, what state must survive, which
// release obligations must complete, whether interruption completed safely, and
// whether (and under what authority) execution may resume. It does not schedule,
// allocate, or place work.
class PreemptionRuntime {
 public:
  PreemptionRuntime(CoordinatorEpoch epoch, PolicyGeneration policy, adapter::ReferenceAdapters& adapters);

  // ---- Execution / authority ----
  void start_execution(WorkloadId w, WorkloadGeneration wg, ExecutionId e, ExecutionGeneration eg, AttemptId a,
                       AttemptGeneration ag, WorkerId worker, WorkerBootId boot);

  // ---- Safe points / progress ----
  SafePointId declare_safe_point(const SafePoint& sp);
  void reach_safe_point(SafePointId id, std::uint64_t position, bool capture_required);
  bool publish_progress(std::uint64_t position, ProgressKind kind);

  // ---- Preemption ----
  RequestAdmission request_preemption(const PreemptionRequest& req);
  PreemptionAssessment assess() const;
  PreemptionResult begin_quiesce();
  PreemptionResult report_quiesced(const QuiescenceReport& report);
  PreemptionResult request_state_capture();
  PreemptionResult report_state_captured(const adapter::CaptureResult& result);
  PreemptionResult request_release();
  PreemptionResult report_released();
  PreemptionResult commit_preempted();

  // ---- Resume / migration compatibility ----
  ResumeEligibility request_resume();
  PreemptionResult revalidate();
  PreemptionResult restore();
  PreemptionResult confirm_resumed();

  // Evaluate an externally supplied migration destination against the preserved
  // state. This never decides placement; it only reports compatibility evidence.
  [[nodiscard]] adapter::MigrationCompatibility evaluate_migration_destination(
      const adapter::DeviceCapability& destination) const;

  // ---- Cancellation / completion ----
  void cancel();
  void complete();

  // ---- Priority-inversion evidence (never schedules; policy input only) ----
  void set_priority_generation(PriorityGeneration g) { priority_generation_ = g; }
  [[nodiscard]] PriorityGeneration current_priority_generation() const { return priority_generation_; }
  [[nodiscard]] PriorityInversionEvidence priority_inversion_assessment(const PriorityInversionInput& input) const;

  // ---- Explainability ----
  std::vector<ExplainEntry> explain_why_not_preemptible() const;
  std::vector<ExplainEntry> explain_resume_blockers() const;
  std::vector<ExplainEntry> explain_state() const;

  // ---- Query ----
  [[nodiscard]] LifecycleState lifecycle() const;
  [[nodiscard]] PreemptibilityClass current_preemptibility() const;
  [[nodiscard]] std::uint64_t durable_progress() const;
  [[nodiscard]] std::uint64_t current_position() const;
  [[nodiscard]] bool is_preempted_durable() const { return machine_.is_preempted_durable(); }
  [[nodiscard]] PreemptionRequestGeneration authoritative_request_generation() const;
  [[nodiscard]] const std::optional<SafePoint>& current_safe_point() const { return current_safe_point_; }
  [[nodiscard]] std::size_t safe_point_count() const { return safe_points_.size(); }

  // Register a fresh worker incarnation (fresh WorkerBootId) for resume.
  void set_worker_incarnation(WorkerId worker, WorkerBootId boot);

  // ---- Authz helpers used by the coordinator ----
  [[nodiscard]] bool is_attempt_authoritative() const;
  [[nodiscard]] CoordinatorEpoch current_epoch() const { return epoch_; }
  [[nodiscard]] WorkerBootId worker_boot() const { return worker_boot_; }
  [[nodiscard]] ExecutionGeneration execution_generation() const { return execution_generation_; }
  [[nodiscard]] AttemptGeneration attempt_generation() const { return attempt_generation_; }
  [[nodiscard]] const adapter::ReferenceAdapters& adapters() const { return adapters_; }

  // ---- Persistence ----
  [[nodiscard]] persist::PersistentState snapshot() const;
  void restore(const persist::PersistentState& state);
  [[nodiscard]] std::vector<std::byte> persist_bytes() const;
  bool restore_bytes(std::span<const std::byte> bytes);
  bool save(const std::string& path) const;
  bool load(const std::string& path);

 private:
  [[nodiscard]] bool validate_authority(const PreemptionRequest& req) const;
  [[nodiscard]] bool request_matches_current(const PreemptionRequest& req) const;
  void validate_attempt(const ExecutionId& exec, const AttemptId& attempt) const;
  [[nodiscard]] bool is_quiesced_all() const;
  void record_explain(std::vector<ExplainEntry>& out, std::string message, std::string detail,
                      EvidenceKind kind = EvidenceKind::kDerived) const;

  mutable std::shared_mutex mutex_;

  CoordinatorEpoch epoch_;
  PolicyGeneration policy_generation_;

  WorkloadId workload_id_;
  WorkloadGeneration workload_generation_;
  ExecutionId execution_id_;
  ExecutionGeneration execution_generation_;
  AttemptId attempt_id_;
  AttemptGeneration attempt_generation_;
  WorkerId worker_id_;
  WorkerBootId worker_boot_;
  WorkerBootId fenced_worker_boot_;

  LifecycleStateMachine machine_;

  std::optional<PreemptionRequest> active_request_;
  PreemptionRequestGeneration authoritative_request_generation_;

  std::map<SafePointId, SafePoint> safe_points_;
  std::optional<SafePoint> current_safe_point_;

  std::uint64_t current_position_ = 0;
  std::uint64_t durable_position_ = 0;
  ProgressGeneration progress_generation_;
  ProgressGeneration durable_progress_generation_;
  std::uint64_t progress_order_ = 0;

  bool quiesced_ = false;
  QuiescenceGeneration quiescence_generation_;

  bool state_durable_ = false;
  StateCaptureGeneration state_capture_generation_;
  CheckpointRef checkpoint_ref_;
  CheckpointGeneration checkpoint_generation_;

  bool resources_released_ = false;
  ReleaseGeneration release_generation_;

  ResumeGeneration resume_generation_;
  bool resumed_ = false;

  std::vector<persist::PersistentSuperseded> history_;

  PriorityGeneration priority_generation_;

  adapter::ReferenceAdapters& adapters_;

  // Lifecycle order helper (numeric enum values are declared in lifecycle order).
  static constexpr std::uint32_t kOrderReleasePending = static_cast<std::uint32_t>(LifecycleState::kReleasePending);
  static constexpr std::uint32_t kOrderResumePending = static_cast<std::uint32_t>(LifecycleState::kResumePending);
};

}  // namespace pf
