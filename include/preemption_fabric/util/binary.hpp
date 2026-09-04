#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

#include "preemption_fabric/util/checked.hpp"

// Deterministic little-endian byte serialization with strict bounds checking.
// Used by both the persistence format and the wire protocol so that every
// field is encoded/decoded identically and never reads or writes out of bounds.
namespace pf::util {

class ByteWriter {
 public:
  ByteWriter() { reserve(256); }

  void reserve(std::size_t n) { buf_.reserve(n); }
  void u8(std::uint8_t v) { push_one(v); }
  void u16(std::uint16_t v) {
    u8(static_cast<std::uint8_t>(v & 0xFFu));
    u8(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  }
  void u32(std::uint32_t v) {
    u8(static_cast<std::uint8_t>(v & 0xFFu));
    u8(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    u8(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    u8(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
  }
  void u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
  }
  // raw little-endian int encoding via bit casts
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }

  void bytes(std::span<const std::byte> data) { buf_.insert(buf_.end(), data.begin(), data.end()); }
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  void string(std::string_view s) {
    if (!util::fits_u32(s.size())) throw std::length_error("string too long for u32 length");
    u32(static_cast<std::uint32_t>(s.size()));
    if (!s.empty()) buf_.insert(buf_.end(), reinterpret_cast<const std::byte*>(s.data()),
                                reinterpret_cast<const std::byte*>(s.data()) + s.size());
  }

  [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
  [[nodiscard]] std::span<const std::byte> data() const noexcept { return buf_; }
  [[nodiscard]] const std::vector<std::byte>& buffer() const noexcept { return buf_; }
  void clear() noexcept { buf_.clear(); }

 private:
  void push_one(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }
  std::vector<std::byte> buf_;
};

class ByteReader {
 public:
  ByteReader() : data_(), pos_(0) {}
  explicit ByteReader(std::span<const std::byte> data) : data_(data), pos_(0) {}
  ByteReader(const std::uint8_t* p, std::size_t n)
      : data_(reinterpret_cast<const std::byte*>(p), n), pos_(0) {}

  void reset(std::span<const std::byte> data) noexcept { data_ = data; pos_ = 0; }

  [[nodiscard]] bool empty() const noexcept { return pos_ >= data_.size(); }
  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
  [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
  [[nodiscard]] bool at_end() const noexcept { return pos_ == data_.size(); }

  bool u8(std::uint8_t& out) {
    if (remaining() < 1) return false;
    out = std::to_integer<std::uint8_t>(data_[pos_++]);
    return true;
  }
  bool u16(std::uint16_t& out) {
    if (remaining() < 2) return false;
    auto lo = std::to_integer<std::uint16_t>(data_[pos_]);
    auto hi = std::to_integer<std::uint16_t>(data_[pos_ + 1]);
    pos_ += 2;
    out = static_cast<std::uint16_t>((hi << 8) | lo);
    return true;
  }
  bool u32(std::uint32_t& out) {
    if (remaining() < 4) return false;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data_[pos_ + i])) << (i * 8);
    pos_ += 4;
    out = v;
    return true;
  }
  bool u64(std::uint64_t& out) {
    if (remaining() < 8) return false;
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data_[pos_ + i])) << (i * 8);
    pos_ += 8;
    out = v;
    return true;
  }
  bool i32(std::int32_t& out) { std::uint32_t u; if (!u32(u)) return false; out = static_cast<std::int32_t>(u); return true; }
  bool i64(std::int64_t& out) { std::uint64_t u; if (!u64(u)) return false; out = static_cast<std::int64_t>(u); return true; }

  bool bytes(std::span<std::byte>& out, std::size_t n) {
    if (remaining() < n) return false;
    out = std::span<std::byte>(const_cast<std::byte*>(data_.data() + pos_), n);
    pos_ += n;
    return true;
  }
  std::optional<std::string_view> string(std::string_view& out) {
    std::uint32_t n;
    if (!u32(n)) return std::nullopt;
    if (!util::fits_u32(n)) return std::nullopt;
    if (static_cast<std::size_t>(n) > remaining()) return std::nullopt;
    const char* p = reinterpret_cast<const char*>(data_.data() + pos_);
    out = std::string_view(p, n);
    pos_ += n;
    return out;
  }

  [[nodiscard]] std::span<const std::byte> remaining_span() const noexcept { return data_.subspan(pos_); }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_;
};

}  // namespace pf::util
