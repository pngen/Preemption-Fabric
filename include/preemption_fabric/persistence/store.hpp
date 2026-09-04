#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "preemption_fabric/persistence/state.hpp"

namespace pf::persist {

// Decode/validation failure categories. Tests and the runtime use these to
// distinguish the many distinct ways a persisted image can be rejected.
enum class PersistenceError : std::uint32_t {
  kOk = 0,
  kBadMagic = 1,
  kBadVersion = 2,
  kTruncated = 3,
  kChecksum = 4,
  kInvalidEnum = 5,
  kMalformedLength = 6,
  kTrailingGarbage = 7,
  kDuplicateId = 8,
  kGenerationRegression = 9,
  kMultipleAuthoritative = 10,
  kIllegalLifecycle = 11,
  kReleaseBeforeQuiescence = 12,
  kResumeBeforePreempted = 13,
  kBrokenCheckpoint = 14,
  kInvalidResourceObligation = 15,
  kMalformedProgress = 16,
  kIntegerOverflow = 17
};

struct DecodeResult {
  bool ok = false;
  PersistenceError error = PersistenceError::kOk;
  PersistentState state;
};

inline constexpr std::string_view to_string(PersistenceError e) noexcept;

// Encode a persistent state image to the versioned, integrity-checked format.
[[nodiscard]] std::vector<std::byte> encode(const PersistentState& state);

// Decode with deterministic validation.
[[nodiscard]] DecodeResult decode(std::span<const std::byte> bytes);

}  // namespace pf::persist

namespace pf::persist {
inline constexpr std::string_view to_string(PersistenceError e) noexcept {
  switch (e) {
    case PersistenceError::kOk: return "OK";
    case PersistenceError::kBadMagic: return "BAD_MAGIC";
    case PersistenceError::kBadVersion: return "BAD_VERSION";
    case PersistenceError::kTruncated: return "TRUNCATED";
    case PersistenceError::kChecksum: return "CHECKSUM";
    case PersistenceError::kInvalidEnum: return "INVALID_ENUM";
    case PersistenceError::kMalformedLength: return "MALFORMED_LENGTH";
    case PersistenceError::kTrailingGarbage: return "TRAILING_GARBAGE";
    case PersistenceError::kDuplicateId: return "DUPLICATE_ID";
    case PersistenceError::kGenerationRegression: return "GENERATION_REGRESSION";
    case PersistenceError::kMultipleAuthoritative: return "MULTIPLE_AUTHORITATIVE";
    case PersistenceError::kIllegalLifecycle: return "ILLEGAL_LIFECYCLE";
    case PersistenceError::kReleaseBeforeQuiescence: return "RELEASE_BEFORE_QUIESCENCE";
    case PersistenceError::kResumeBeforePreempted: return "RESUME_BEFORE_PREEMPTED";
    case PersistenceError::kBrokenCheckpoint: return "BROKEN_CHECKPOINT";
    case PersistenceError::kInvalidResourceObligation: return "INVALID_RESOURCE_OBLIGATION";
    case PersistenceError::kMalformedProgress: return "MALFORMED_PROGRESS";
    case PersistenceError::kIntegerOverflow: return "INTEGER_OVERFLOW";
  }
  return "UNKNOWN";
}
}  // namespace pf::persist
