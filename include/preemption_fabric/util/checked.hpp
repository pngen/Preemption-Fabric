#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

// Checked arithmetic helpers. Overflow is never silent: every size/count that
// can be driven by untrusted input goes through these helpers so a malicious
// declared length cannot wrap and bypass bounds checks.
namespace pf::util {

template <typename T>
[[nodiscard]] constexpr std::optional<T> checked_add(T a, T b) noexcept {
  static_assert(std::numeric_limits<T>::is_integer, "integer type required");
  if (b > 0 && a > std::numeric_limits<T>::max() - b) return std::nullopt;
  if (b < 0 && a < std::numeric_limits<T>::min() - b) return std::nullopt;
  return static_cast<T>(a + b);
}

template <typename T>
[[nodiscard]] constexpr std::optional<T> checked_mul(T a, T b) noexcept {
  static_assert(std::numeric_limits<T>::is_integer, "integer type required");
  if (a == 0 || b == 0) return static_cast<T>(0);
  if (a == -1 && b == std::numeric_limits<T>::min()) return std::nullopt;
  if (b == -1 && a == std::numeric_limits<T>::min()) return std::nullopt;
  if (a > 0) {
    if (b > 0) { if (a > std::numeric_limits<T>::max() / b) return std::nullopt; }
    else { if (b < std::numeric_limits<T>::min() / a) return std::nullopt; }
  } else {
    if (b > 0) { if (a < std::numeric_limits<T>::min() / b) return std::nullopt; }
    else { if (a != 0 && b < std::numeric_limits<T>::max() / a) return std::nullopt; }
  }
  return static_cast<T>(a * b);
}

[[nodiscard]] constexpr bool fits_u32(std::uint64_t v) noexcept {
  return v <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
}

[[nodiscard]] constexpr bool fits_u16(std::uint64_t v) noexcept {
  return v <= static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max());
}

[[nodiscard]] constexpr bool fits_u8(std::uint64_t v) noexcept {
  return v <= static_cast<std::uint64_t>(std::numeric_limits<std::uint8_t>::max());
}

}  // namespace pf::util
