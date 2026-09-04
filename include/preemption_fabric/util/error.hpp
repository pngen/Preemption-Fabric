#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace pf {

// Logical error raised by the runtime when an illegal transition or a violated
// invariant is detected (e.g. rejecting an illegal lifecycle transition). It is
// distinct from a legitimate preemption *outcome* (e.g. DEFER_TO_SAFE_POINT),
// which is returned as a typed enum, never thrown.
class PreemptionError : public std::runtime_error {
 public:
  PreemptionError(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code_(std::move(code)) {}
  [[nodiscard]] const std::string& code() const noexcept { return code_; }

 private:
  std::string code_;
};

[[noreturn]] inline void throw_invalid_transition(const char* from, const char* to) {
  throw PreemptionError("illegal_transition",
                        std::string("illegal lifecycle transition from ") + from + " to " + to);
}

}  // namespace pf
