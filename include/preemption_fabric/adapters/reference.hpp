#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "preemption_fabric/adapters/interfaces.hpp"

namespace pf::adapter {

// Deterministic reference implementations of the narrow adjacent-runtime
// interfaces. These are used by standalone tests, examples, and the reference
// coordinator so that the runtime is fully exercisable without coupling to any
// sibling repository. They expose narrow setters so tests can drive every
// success/failure/stale path deterministically.
class ReferenceAdapters final
    : public IExecutionFabric,
      public IWorkloadFabric,
      public ICheckpointFabric,
      public IResourceBroker,
      public IDependencyFabric,
      public IFailureFabric,
      public IQuiescenceProvider,
      public ICompatibilityEvaluator {
 public:
  // --- Execution Fabric ---
  void set_authoritative(ExecutionId execution, AttemptId attempt, ExecutionGeneration gen,
                         AttemptGeneration attempt_gen, CoordinatorEpoch epoch, WorkerBootId worker) {
    attempt_authoritative_ = true;
    attempt_execution_ = execution;
    attempt_attempt_ = attempt;
    attempt_gen_ = gen;
    attempt_boot_ = worker;
    attempt_epoch_ = epoch;
    attempt_attempt_gen_ = attempt_gen;
    attempt_provenance_ = EvidenceKind::kReal;
  }
  void revoke_authority() { attempt_authoritative_ = false; }

  AttemptAuthorityView current_attempt_authority(ExecutionId execution, AttemptId attempt) const override {
    AttemptAuthorityView v;
    v.execution_id = execution;
    v.attempt_id = attempt;
    v.execution_generation = attempt_gen_;
    v.attempt_generation = attempt_attempt_gen_;
    v.coordinator_epoch = attempt_epoch_;
    v.worker_boot_id = attempt_boot_;
    v.provenance = attempt_provenance_;
    v.authoritative = attempt_authoritative_ && execution == attempt_execution_ && attempt == attempt_attempt_;
    return v;
  }
  bool fence_attempt(ExecutionId, AttemptId, ExecutionGeneration) override {
    attempt_authoritative_ = false;
    return true;
  }
  AttemptAuthorityView grant_fresh_attempt_authority(ExecutionId execution, AttemptId attempt,
                                                     ExecutionGeneration gen, WorkerBootId worker) override {
    // A fresh resume authority always has a newer attempt generation.
    attempt_attempt_gen_ = attempt_attempt_gen_.next();
    attempt_authoritative_ = true;
    attempt_execution_ = execution;
    attempt_attempt_ = attempt;
    attempt_gen_ = gen;
    attempt_boot_ = worker;
    attempt_provenance_ = EvidenceKind::kReal;
    AttemptAuthorityView v;
    v.execution_id = execution;
    v.attempt_id = attempt;
    v.execution_generation = gen;
    v.attempt_generation = attempt_attempt_gen_;
    v.coordinator_epoch = attempt_epoch_;
    v.worker_boot_id = worker;
    v.authoritative = true;
    v.provenance = EvidenceKind::kReal;
    return v;
  }

  // --- Workload Fabric ---
  void set_workload_state(WorkloadId w, WorkloadLifecycleState s) { workload_state_[w] = s; }
  WorkloadLifecycleState workload_state(WorkloadId w) const override {
    auto it = workload_state_.find(w);
    return it == workload_state_.end() ? WorkloadLifecycleState::kUnknown : it->second;
  }
  void notify_preempted(WorkloadId w, WorkloadGeneration, ExecutionId, ExecutionGeneration) override {
    workload_state_[w] = WorkloadLifecycleState::kPreempted;
  }
  void notify_resumed(WorkloadId w, WorkloadGeneration, ExecutionId, ExecutionGeneration, ResumeGeneration) override {
    workload_state_[w] = WorkloadLifecycleState::kActive;
  }

  // --- Checkpoint Fabric ---
  void set_capture_succeeds(bool v) { capture_default_ = v; }
  void set_capture_count(std::uint64_t v) { capture_count_ = v; }

  CaptureResult request_capture(const StateCaptureRequest& req) override {
    ++capture_count_;
    CaptureResult r;
    r.capture_id = StateCaptureId(static_cast<std::uint64_t>(capture_count_));
    r.capture_generation = CaptureGeneration_ = req.capture_generation;
    r.checkpoint_generation = req.expected_generation;
    r.checkpoint_ref = CheckpointRef(static_cast<std::uint64_t>(capture_count_));
    r.completed = true;
    r.success = capture_default_;
    r.provenance = EvidenceKind::kReal;
    last_capture_ = r;
    return r;
  }
  [[nodiscard]] const std::optional<CaptureResult>& last_capture() const { return last_capture_; }
  CaptureResult query_capture(StateCaptureId) override {
    CaptureResult r;
    r.completed = true;
    r.success = capture_default_;
    r.provenance = EvidenceKind::kReal;
    return r;
  }
  bool validate_checkpoint(CheckpointRef ref, CheckpointGeneration expected) const override {
    // Generation fencing: an old checkpoint generation must not satisfy a
    // current validation. The ref must be a real (non-default) checkpoint.
    return checkpoint_valid_ && ref.is_valid() && expected == current_checkpoint_gen_;
  }
  // Advance/set the authoritative checkpoint generation (models supersession).
  void set_current_checkpoint_generation(CheckpointGeneration g) { current_checkpoint_gen_ = g; }
  void set_checkpoint_valid(bool v) { checkpoint_valid_ = v; }
  bool restore(CheckpointRef ref, CheckpointGeneration expected) override {
    return checkpoint_valid_ && ref.is_valid() && expected == current_checkpoint_gen_;
  }

  // --- Resource Broker (exact accounting) ---
  void set_contract(ExecutionId e, ResourceContractGeneration g, std::vector<ResourceLease> leases) {
    contract_id_ = ResourceContractId(random_id_++);
    contract_gen_ = g;
    contract_execution_ = e;
    reserved_.clear();
    released_.clear();
    for (auto& lease : leases) reserved_[lease.resource] += lease.units;
  }
  void advance_contract(std::uint64_t by = 1) { contract_gen_ = Generation_advance(contract_gen_, by); }

  ResourceContract current_contract(ExecutionId) const override {
    ResourceContract c;
    c.id = contract_id_;
    c.generation = contract_gen_;
    for (auto& [rc, n] : reserved_) c.leases.push_back({rc, n});
    return c;
  }
  ReleaseResult request_release(ExecutionId e, ResourceClass rc, std::uint64_t units,
                                ResourceContractGeneration expected) override {
    ReleaseResult r;
    r.release_generation = contract_gen_;
    r.worker_boot_id = attempt_boot_;
    if (e != contract_execution_ || expected != contract_gen_) {
      r.success = false;  // stale contract or wrong execution
      return r;
    }
    auto it = reserved_.find(rc);
    if (it == reserved_.end() || it->second < units) {
      r.success = false;
      return r;
    }
    it->second -= units;
    released_[rc] += units;
    r.success = true;
    return r;
  }
  ReacquireResult reacquire(ExecutionId e, ResourceClass rc, std::uint64_t units,
                            ResourceContractGeneration contract) override {
    ReacquireResult r;
    // Honor an explicitly supplied contract generation, otherwise advance.
    r.contract_generation = contract_gen_ =
        (contract.is_valid() ? contract : Generation_advance(contract_gen_, 1));
    r.worker_boot_id = attempt_boot_;
    if (e != contract_execution_) {
      r.success = false;
      return r;
    }
    if (released_[rc] < units) {
      r.success = false;
      return r;
    }
    released_[rc] -= units;
    reserved_[rc] += units;
    r.success = true;
    return r;
  }

  // --- Dependency Fabric ---
  void set_dependency_status(DependencyId d, DependencyStatus s, DependencyGeneration g = {}, bool rv = false,
                             bool rc = false) {
    dep_status_[d] = {s, g, rv, rc};
  }
  void set_resume_dependency(DependencyStatus s, DependencyGeneration g = {}, bool rv = false, bool rc = false) {
    resume_dep_ = {s, g, rv, rc};
  }
  DependencyView dependency_status(DependencyId d) override {
    auto it = dep_status_.find(d);
    return it == dep_status_.end() ? DependencyView{} : it->second;
  }
  DependencyView resume_dependencies(ExecutionId) override { return resume_dep_; }

  // --- Compatibility evaluator (migration destination) ---
  void set_capability_context(DeviceCapability current, DeviceCapability required) {
    current_capability_ = current;
    required_capability_ = required;
  }
  void set_force_incompatible(bool v) { force_incompatible_ = v; }
  void set_force_stale_capability(bool v) { force_stale_capability_ = v; }
  DeviceCapability current_capability() const override { return current_capability_; }
  bool capability_is_compatible(const DeviceCapability& dst) const override {
    if (force_incompatible_) return false;
    return dst.memory_bytes >= required_capability_.memory_bytes && dst.supports_state_restore &&
           dst.compute_compatibility >= required_capability_.compute_compatibility;
  }
  bool capability_generation_current(const DeviceCapability& dst) const override {
    if (force_stale_capability_) return false;
    return dst.capability_generation == current_capability_.capability_generation;
  }

  // --- Failure Fabric ---
  void report(FailureCategory category, std::string_view detail) override {
    failures_.push_back({category, std::string(detail)});
  }
  [[nodiscard]] const std::vector<std::pair<FailureCategory, std::string>>& failure_log() const { return failures_; }

  // --- Quiescence ---
  void set_quiesced(QuiescenceCategory c, bool v) { quiesced_[c] = v; }
  bool is_quiesced(QuiescenceCategory c) const override {
    auto it = quiesced_.find(c);
    return it != quiesced_.end() ? it->second : false;
  }
  std::vector<QuiescenceCategory> categories() const override {
    static const std::vector<QuiescenceCategory> all = {
        QuiescenceCategory::kCpuWorkerActivity, QuiescenceCategory::kAcceleratorKernels,
        QuiescenceCategory::kAsynchronousCopies, QuiescenceCategory::kQueuedDeviceOperations,
        QuiescenceCategory::kHostCallbacks, QuiescenceCategory::kStorageWrites,
        QuiescenceCategory::kNetworkActivity, QuiescenceCategory::kStatePublication,
        QuiescenceCategory::kCompletionPublication, QuiescenceCategory::kPendingResourceMutation};
    return all;
  }

  // --- Accounting inspection (for exact-accounting tests) ---
  [[nodiscard]] std::uint64_t reserved(ResourceClass c) const { return lookup(reserved_, c); }
  [[nodiscard]] std::uint64_t released(ResourceClass c) const { return lookup(released_, c); }
  [[nodiscard]] ResourceContractGeneration contract_generation() const { return contract_gen_; }

 private:
  static std::uint64_t lookup(const std::map<ResourceClass, std::uint64_t>& m, ResourceClass c) {
    auto it = m.find(c);
    return it == m.end() ? 0 : it->second;
  }
  static ResourceContractGeneration Generation_advance(ResourceContractGeneration g, std::uint64_t by) {
    return ResourceContractGeneration(g.value() + by);
  }
  // Execution authority
  bool attempt_authoritative_ = false;
  ExecutionId attempt_execution_;
  AttemptId attempt_attempt_;
  ExecutionGeneration attempt_gen_;
  AttemptGeneration attempt_attempt_gen_;
  CoordinatorEpoch attempt_epoch_;
  WorkerBootId attempt_boot_;
  EvidenceKind attempt_provenance_ = EvidenceKind::kUnknown;

  // Workload
  std::map<WorkloadId, WorkloadLifecycleState> workload_state_;

  // Checkpoint
  bool capture_default_ = true;
  std::uint64_t capture_count_ = 0;
  bool checkpoint_valid_ = true;
  CheckpointGeneration current_checkpoint_gen_{1};
  StateCaptureGeneration CaptureGeneration_;
  std::optional<CaptureResult> last_capture_;

  // Resource broker accounting
  ResourceContractId contract_id_;
  ResourceContractGeneration contract_gen_;
  ExecutionId contract_execution_;
  std::map<ResourceClass, std::uint64_t> reserved_;
  std::map<ResourceClass, std::uint64_t> released_;
  std::uint64_t random_id_ = 1;

  // Dependency
  std::map<DependencyId, DependencyView> dep_status_;
  DependencyView resume_dep_;

  // Compatibility
  DeviceCapability current_capability_;
  DeviceCapability required_capability_;
  bool force_incompatible_ = false;
  bool force_stale_capability_ = false;

  // Failure
  std::vector<std::pair<FailureCategory, std::string>> failures_;

  // Quiescence
  std::map<QuiescenceCategory, bool> quiesced_;
};

}  // namespace pf::adapter
