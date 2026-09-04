#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

// Generic enum <-> string conversion helpers used throughout Preemption Fabric.
// Each strongly typed enum defines a constexpr std::array of {value, name} pairs
// and a thin to_string / from_string wrapper (see core/enums.hpp).
namespace pf::detail {

template <typename E, std::size_t N>
[[nodiscard]] constexpr std::string_view name_of(E value,
                                                 const std::array<std::pair<E, std::string_view>, N>& table) noexcept {
  for (const auto& entry : table) {
    if (entry.first == value) return entry.second;
  }
  return "<unknown>";
}

template <typename E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> value_of(std::string_view name,
                                                  const std::array<std::pair<E, std::string_view>, N>& table) noexcept {
  for (const auto& entry : table) {
    if (entry.second == name) return entry.first;
  }
  return std::nullopt;
}

}  // namespace pf::detail

// Expands to a table entry used inside std::to_array<std::pair<EnumType, std::string_view>>.
#define PF_ENUM_ENTRY(EnumType, Value, DisplayName) \
  { EnumType::Value, DisplayName }
