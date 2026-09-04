#pragma once

#include <compare>
#include <cstdint>
#include <ostream>

// Strongly typed authority identities and generations.
//
// Preemption Fabric refuses to collapse semantically different authority domains
// into a generic integer. Every identity domain is a distinct C++ type; a value
// from one domain cannot silently be used where another is required. Both Id and
// Generation templates are tagged by the same domain Tag, but they remain
// distinct types (an Id selects an entity, a Generation selects a version).
//
// An old PreemptionRequestGeneration must never authorize a new execution
// attempt. An old WorkerBootId must never publish quiescence/release evidence
// after worker reincarnation. These invariants are enforced by type, then at
// runtime by generation checks in the domain logic.
namespace pf {

namespace detail {
template <typename Tag>
constexpr auto tag_name() noexcept {
  // Kept intentionally simple: no RTTI, no external deps, deterministic names.
  return "";
}
}  // namespace detail

template <typename Tag>
class Generation {
 public:
  using TagType = Tag;
  using ValueType = std::uint64_t;

  static constexpr ValueType kInvalidValue = 0u;

  constexpr Generation() noexcept = default;
  explicit constexpr Generation(ValueType value) noexcept : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != kInvalidValue; }
  [[nodiscard]] constexpr bool is_default() const noexcept { return value_ == kInvalidValue; }

  [[nodiscard]] constexpr Generation next() const noexcept {
    // Saturating increment. Overflow is rejected at a higher layer (never here).
    if (value_ == kMaxValue) return *this;
    return Generation(value_ + 1u);
  }

  friend constexpr bool operator==(Generation, Generation) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Generation a, Generation b) noexcept {
    return a.value_ <=> b.value_;
  }

 private:
  static constexpr ValueType kMaxValue = ~ValueType{0};
  ValueType value_{kInvalidValue};
};

template <typename Tag>
class Id {
 public:
  using TagType = Tag;
  using ValueType = std::uint64_t;

  static constexpr ValueType kInvalidValue = 0u;

  constexpr Id() noexcept = default;
  explicit constexpr Id(ValueType value) noexcept : value_(value) {}

  [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != kInvalidValue; }
  [[nodiscard]] constexpr bool is_default() const noexcept { return value_ == kInvalidValue; }
  [[nodiscard]] constexpr Id next() const noexcept { return Id(value_ == ~ValueType{0} ? value_ : value_ + 1u); }

  friend constexpr bool operator==(Id, Id) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Id a, Id b) noexcept { return a.value_ <=> b.value_; }

 private:
  ValueType value_{kInvalidValue};
};

// Identity domain tags. Each tag is an empty type used only to give Id<Tag> /
// Generation<Tag> distinct, non-interchangeable C++ types.
#define PF_DEFINE_ID_TAG(TagName) struct TagName##Tag {}

// ---------------------------------------------------------------------------
// Preemption request domain
// ---------------------------------------------------------------------------
struct PreemptionRequestDomain {};
using PreemptionRequestId = Id<PreemptionRequestDomain>;
using PreemptionRequestGeneration = Generation<PreemptionRequestDomain>;

// ---------------------------------------------------------------------------
// Preemption plan domain
// ---------------------------------------------------------------------------
struct PreemptionPlanDomain {};
using PreemptionPlanId = Id<PreemptionPlanDomain>;
using PreemptionPlanGeneration = Generation<PreemptionPlanDomain>;

// ---------------------------------------------------------------------------
// Preemption attempt domain
// ---------------------------------------------------------------------------
struct PreemptionAttemptDomain {};
using PreemptionAttemptId = Id<PreemptionAttemptDomain>;
using PreemptionAttemptGeneration = Generation<PreemptionAttemptDomain>;

// ---------------------------------------------------------------------------
// Workload domain
// ---------------------------------------------------------------------------
struct WorkloadDomain {};
using WorkloadId = Id<WorkloadDomain>;
using WorkloadGeneration = Generation<WorkloadDomain>;

// ---------------------------------------------------------------------------
// Execution domain
// ---------------------------------------------------------------------------
struct ExecutionDomain {};
using ExecutionId = Id<ExecutionDomain>;
using ExecutionGeneration = Generation<ExecutionDomain>;

// ---------------------------------------------------------------------------
// Attempt domain (an execution attempt)
// ---------------------------------------------------------------------------
struct AttemptDomain {};
using AttemptId = Id<AttemptDomain>;
using AttemptGeneration = Generation<AttemptDomain>;

// ---------------------------------------------------------------------------
// Worker domain
// ---------------------------------------------------------------------------
struct WorkerDomain {};
using WorkerId = Id<WorkerDomain>;
using WorkerBootId = Id<WorkerDomain>;  // reincarnation id (fresh each boot)

// ---------------------------------------------------------------------------
// Coordinator domain
// ---------------------------------------------------------------------------
struct CoordinatorDomain {};
using CoordinatorId = Id<CoordinatorDomain>;
using CoordinatorEpoch = Generation<CoordinatorDomain>;

// ---------------------------------------------------------------------------
// Safe point domain
// ---------------------------------------------------------------------------
struct SafePointDomain {};
using SafePointId = Id<SafePointDomain>;
using SafePointGeneration = Generation<SafePointDomain>;

// ---------------------------------------------------------------------------
// Quiescence domain
// ---------------------------------------------------------------------------
struct QuiescenceDomain {};
using QuiescenceGeneration = Generation<QuiescenceDomain>;

// ---------------------------------------------------------------------------
// Progress domain
// ---------------------------------------------------------------------------
struct ProgressDomain {};
using ProgressGeneration = Generation<ProgressDomain>;

// ---------------------------------------------------------------------------
// State capture domain
// ---------------------------------------------------------------------------
struct StateCaptureDomain {};
using StateCaptureId = Id<StateCaptureDomain>;
using StateCaptureGeneration = Generation<StateCaptureDomain>;

// ---------------------------------------------------------------------------
// Checkpoint domain
// ---------------------------------------------------------------------------
struct CheckpointDomain {};
using CheckpointRef = Id<CheckpointDomain>;
using CheckpointGeneration = Generation<CheckpointDomain>;

// ---------------------------------------------------------------------------
// Preservation domain
// ---------------------------------------------------------------------------
struct PreservationDomain {};
using PreservationGeneration = Generation<PreservationDomain>;

// ---------------------------------------------------------------------------
// Resource contract domain
// ---------------------------------------------------------------------------
struct ResourceContractDomain {};
using ResourceContractId = Id<ResourceContractDomain>;
using ResourceContractGeneration = Generation<ResourceContractDomain>;
using ReleaseGeneration = Generation<ResourceContractDomain>;

// ---------------------------------------------------------------------------
// Resume domain
// ---------------------------------------------------------------------------
struct ResumeDomain {};
using ResumeId = Id<ResumeDomain>;
using ResumeGeneration = Generation<ResumeDomain>;

// ---------------------------------------------------------------------------
// Dependency domain
// ---------------------------------------------------------------------------
struct DependencyDomain {};
using DependencyId = Id<DependencyDomain>;
using DependencyGeneration = Generation<DependencyDomain>;

// ---------------------------------------------------------------------------
// Priority domain
// ---------------------------------------------------------------------------
struct PriorityDomain {};
using PriorityGeneration = Generation<PriorityDomain>;

// ---------------------------------------------------------------------------
// Policy domain
// ---------------------------------------------------------------------------
struct PolicyDomain {};
using PolicyGeneration = Generation<PolicyDomain>;

// ---------------------------------------------------------------------------
// Authority domain
// ---------------------------------------------------------------------------
struct AuthorityDomain {};
using AuthorityGeneration = Generation<AuthorityDomain>;

}  // namespace pf
