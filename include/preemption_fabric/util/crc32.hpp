#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Deterministic CRC-32 (IEEE 802.3, reflected) used for frame and persistence
// integrity. Integrity checking is deterministic and platform independent so
// that the release produces the same checksums everywhere.
namespace pf::util {

constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320u;

// Compile-time table for the reflected CRC-32 (not strictly constexpr on all
// toolchains without constexpr loops, so it is computed once at first use).
inline std::uint32_t crc32(std::span<const std::byte> data) noexcept {
  static std::uint32_t table[256] = {};
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? (kCrc32Polynomial ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::byte b : data) {
    crc = table[(crc ^ std::to_integer<std::uint8_t>(b)) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

inline std::uint32_t crc32(const std::uint8_t* p, std::size_t n) noexcept {
  return crc32(std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), n));
}

}  // namespace pf::util
