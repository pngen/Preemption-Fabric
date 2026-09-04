#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "preemption_fabric/protocol/message.hpp"

namespace pf::proto {

// Frame rejection categories. Decoding is deterministic and rejects truncated,
// corrupt, over-long, unknown-type, and trailing-garbage frames.
enum class FrameError : std::uint32_t {
  kOk = 0,
  kTruncated = 1,
  kBadMagic = 2,
  kBadVersion = 3,
  kBadLength = 4,
  kChecksum = 5,
  kBadMessageType = 6,
  kInvalidEnum = 7,
  kMalformedBody = 8,
  kTrailingGarbage = 9
};

inline constexpr std::string_view to_string(FrameError e) noexcept {
  switch (e) {
    case FrameError::kOk: return "OK";
    case FrameError::kTruncated: return "TRUNCATED";
    case FrameError::kBadMagic: return "BAD_MAGIC";
    case FrameError::kBadVersion: return "BAD_VERSION";
    case FrameError::kBadLength: return "BAD_LENGTH";
    case FrameError::kChecksum: return "CHECKSUM";
    case FrameError::kBadMessageType: return "BAD_MESSAGE_TYPE";
    case FrameError::kInvalidEnum: return "INVALID_ENUM";
    case FrameError::kMalformedBody: return "MALFORMED_BODY";
    case FrameError::kTrailingGarbage: return "TRAILING_GARBAGE";
  }
  return "UNKNOWN";
}

struct FrameDecodeResult {
  bool ok = false;
  FrameError error = FrameError::kOk;
  Message message;
};

// Max allowed frame body length; an untrusted declared length can never drive an
// unbounded allocation.
inline constexpr std::uint32_t kMaxFrameBody = 64u * 1024u;
inline constexpr std::uint32_t kMagic = 0x31524650u;  // 'P','F','R','1' little-endian
// Header is: magic(4) + version(4) + type(4) + seq(4) + body_len(4) = 20 bytes.
// The CRC-32 trailer (4 bytes) follows the body.
inline constexpr std::uint32_t kFrameHeaderSize = 20u;

// Encode a message to a complete framed message (header + body + crc).
[[nodiscard]] std::vector<std::byte> encode_frame(const Message& msg);

// Decode a single complete frame that must start exactly at a frame boundary.
[[nodiscard]] FrameDecodeResult decode_frame(std::span<const std::byte> bytes);

// Streaming decoder: feed arbitrary chunks (as produced by partial TCP reads)
// and pop complete, validated frames. Bounded internal buffering.
class FrameDecoder {
 public:
  FrameDecoder() { buffer_.reserve(1024); }

  void feed(std::span<const std::byte> data) { buffer_.insert(buffer_.end(), data.begin(), data.end()); }

  // Pop the next complete frame, or nullopt if a full frame is not yet buffered.
  std::optional<Message> pop();

  void reset() { buffer_.clear(); fatal_ = false; }
  [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size(); }
  // True if a fatal protocol error was observed (bad magic/version/length/crc).
  [[nodiscard]] bool fatal() const noexcept { return fatal_; }

 private:
  void set_fatal() { fatal_ = true; buffer_.clear(); }
  std::vector<std::byte> buffer_;
  bool fatal_ = false;
};

}  // namespace pf::proto
