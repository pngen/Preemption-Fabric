#include "preemption_fabric/protocol/framing.hpp"

#include <cstring>

#include "preemption_fabric/util/binary.hpp"
#include "preemption_fabric/util/crc32.hpp"
#include "preemption_fabric/version.hpp"

namespace pf::proto {
namespace {

// Encode the message body (all fields) as a deterministic, bounds-checked blob.
util::ByteWriter encode_body(const Message& m) {
  util::ByteWriter w;
  w.u32(static_cast<std::uint32_t>(m.type));
  w.u32(m.seq);
  w.u64(m.coordinator_epoch.value());
  w.u64(m.worker_id.value());
  w.u64(m.worker_boot_id.value());
  w.u64(m.execution_id.value());
  w.u64(m.execution_generation.value());
  w.u64(m.attempt_id.value());
  w.u64(m.attempt_generation.value());
  w.u64(m.request_id.value());
  w.u64(m.request_generation.value());
  w.u64(m.worker_b.value());
  w.u64(m.progress_position);
  w.u64(m.progress_generation.value());
  w.u32(static_cast<std::uint32_t>(m.progress_kind));
  w.u64(m.safe_point_id.value());
  w.u64(m.safe_point_generation.value());
  w.u64(m.safe_point_position);
  w.u8(m.safe_point_capture_required ? 1 : 0);
  w.u64(m.checkpoint_ref.value());
  w.u64(m.checkpoint_generation.value());
  w.u64(m.capture_id.value());
  w.u64(m.capture_generation.value());
  w.u64(m.resume_id.value());
  w.u64(m.resume_generation.value());
  w.u32(static_cast<std::uint32_t>(m.resource));
  w.u64(m.units);
  w.u64(m.contract_id.value());
  w.u64(m.contract_generation.value());
  w.u32(static_cast<std::uint32_t>(m.reason));
  w.u32(static_cast<std::uint32_t>(m.outcome));
  w.u32(static_cast<std::uint32_t>(m.lifecycle));
  w.u32(static_cast<std::uint32_t>(m.preemptibility));
  w.u32(static_cast<std::uint32_t>(m.resume_outcome));
  w.u32(static_cast<std::uint32_t>(m.dep_status));
  w.u32(static_cast<std::uint32_t>(m.preservation_outcome));
  w.string(m.detail);
  w.string(m.extra);
  return w;
}

bool read_u64(util::ByteReader& r, std::uint64_t& v) { return r.u64(v); }
bool read_u32(util::ByteReader& r, std::uint32_t& v) { return r.u32(v); }
bool read_u8(util::ByteReader& r, std::uint8_t& v) { return r.u8(v); }

bool decode_body(std::span<const std::byte> bytes, Message& out) {
  util::ByteReader r(bytes);
  std::uint32_t t, seq;
  std::uint64_t v;
  if (!read_u32(r, t)) return false;
  if (!read_u32(r, seq)) return false;
  if (t == 0 || t > static_cast<std::uint32_t>(MessageType::kReplayStale)) return false;
  out.type = static_cast<MessageType>(t);
  out.seq = seq;

  if (!read_u64(r, v)) return false; out.coordinator_epoch = CoordinatorEpoch(v);
  if (!read_u64(r, v)) return false; out.worker_id = WorkerId(v);
  if (!read_u64(r, v)) return false; out.worker_boot_id = WorkerBootId(v);
  if (!read_u64(r, v)) return false; out.execution_id = ExecutionId(v);
  if (!read_u64(r, v)) return false; out.execution_generation = ExecutionGeneration(v);
  if (!read_u64(r, v)) return false; out.attempt_id = AttemptId(v);
  if (!read_u64(r, v)) return false; out.attempt_generation = AttemptGeneration(v);
  if (!read_u64(r, v)) return false; out.request_id = PreemptionRequestId(v);
  if (!read_u64(r, v)) return false; out.request_generation = PreemptionRequestGeneration(v);
  if (!read_u64(r, v)) return false; out.worker_b = WorkerId(v);
  if (!read_u64(r, v)) return false; out.progress_position = v;
  if (!read_u64(r, v)) return false; out.progress_generation = ProgressGeneration(v);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(ProgressKind::kLost)) return false;
  out.progress_kind = static_cast<ProgressKind>(t);
  if (!read_u64(r, v)) return false; out.safe_point_id = SafePointId(v);
  if (!read_u64(r, v)) return false; out.safe_point_generation = SafePointGeneration(v);
  if (!read_u64(r, v)) return false; out.safe_point_position = v;
  std::uint8_t u8v;
  if (!read_u8(r, u8v)) return false; out.safe_point_capture_required = u8v != 0;
  if (!read_u64(r, v)) return false; out.checkpoint_ref = CheckpointRef(v);
  if (!read_u64(r, v)) return false; out.checkpoint_generation = CheckpointGeneration(v);
  if (!read_u64(r, v)) return false; out.capture_id = StateCaptureId(v);
  if (!read_u64(r, v)) return false; out.capture_generation = StateCaptureGeneration(v);
  if (!read_u64(r, v)) return false; out.resume_id = ResumeId(v);
  if (!read_u64(r, v)) return false; out.resume_generation = ResumeGeneration(v);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(ResourceClass::kOther)) return false;
  out.resource = static_cast<ResourceClass>(t);
  if (!read_u64(r, v)) return false; out.units = v;
  if (!read_u64(r, v)) return false; out.contract_id = ResourceContractId(v);
  if (!read_u64(r, v)) return false; out.contract_generation = ResourceContractGeneration(v);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(PreemptionReason::kUnknown)) return false;
  out.reason = static_cast<PreemptionReason>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(PreemptionOutcome::kPreemptionFailed)) return false;
  out.outcome = static_cast<PreemptionOutcome>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(LifecycleState::kSuperseded)) return false;
  out.lifecycle = static_cast<LifecycleState>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(PreemptibilityClass::kUnknown)) return false;
  out.preemptibility = static_cast<PreemptibilityClass>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(ResumeOutcome::kUnknown)) return false;
  out.resume_outcome = static_cast<ResumeOutcome>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(adapter::DependencyStatus::kUnknown)) return false;
  out.dep_status = static_cast<adapter::DependencyStatus>(t);
  if (!read_u32(r, t)) return false;
  if (t > static_cast<std::uint32_t>(StatePreservationOutcome::kUnknown)) return false;
  out.preservation_outcome = static_cast<StatePreservationOutcome>(t);
  std::string_view sv;
  if (!r.string(sv).has_value()) return false;
  out.detail.assign(sv.data(), sv.size());
  if (!r.string(sv).has_value()) return false;
  out.extra.assign(sv.data(), sv.size());
  return r.at_end();
}

}  // namespace

std::vector<std::byte> encode_frame(const Message& m) {
  util::ByteWriter body = encode_body(m);
  std::vector<std::byte> out;
  util::ByteWriter hw;
  hw.u32(kMagic);
  hw.u32(PF_PROTOCOL_VERSION);
  hw.u32(static_cast<std::uint32_t>(m.type));
  hw.u32(m.seq);
  if (!util::fits_u32(body.size())) throw std::length_error("message too large for frame");
  hw.u32(static_cast<std::uint32_t>(body.size()));
  out.insert(out.end(), hw.data().begin(), hw.data().end());
  out.insert(out.end(), body.data().begin(), body.data().end());
  std::uint32_t crc = util::crc32(std::span<const std::byte>(out.data(), out.size()));
  util::ByteWriter cw;
  cw.u32(crc);
  out.insert(out.end(), cw.data().begin(), cw.data().end());
  return out;
}

FrameDecodeResult decode_frame(std::span<const std::byte> bytes) {
  FrameDecodeResult res;
  const auto fail = [&](FrameError e) {
    FrameDecodeResult r;
    r.ok = false;
    r.error = e;
    return r;
  };
  if (bytes.size() < kFrameHeaderSize + 4u) return fail(FrameError::kTruncated);
  util::ByteReader hr(bytes.first(kFrameHeaderSize));
  std::uint32_t magic = 0, version = 0, type = 0, seq = 0, body_len = 0;
  if (!hr.u32(magic)) return fail(FrameError::kTruncated);
  if (magic != kMagic) return fail(FrameError::kBadMagic);
  if (!hr.u32(version)) return fail(FrameError::kTruncated);
  if (version != PF_PROTOCOL_VERSION) return fail(FrameError::kBadVersion);
  if (!hr.u32(type)) return fail(FrameError::kTruncated);
  if (!hr.u32(seq)) return fail(FrameError::kTruncated);
  if (!hr.u32(body_len)) return fail(FrameError::kTruncated);
  if (body_len > kMaxFrameBody) return fail(FrameError::kBadLength);
  const std::size_t body_start = kFrameHeaderSize;
  const std::size_t body_end = body_start + static_cast<std::size_t>(body_len);
  const std::size_t total = body_end + 4u;
  if (bytes.size() < total) return fail(FrameError::kTruncated);
  if (bytes.size() > total) return fail(FrameError::kTrailingGarbage);

  // CRC-32 trailer covers header + body (everything before the trailer).
  std::uint32_t stored_crc = 0;
  {
    util::ByteReader cr(bytes.subspan(body_end, 4));
    if (!cr.u32(stored_crc)) return fail(FrameError::kTruncated);
  }
  std::uint32_t cfull = util::crc32(std::span<const std::byte>(bytes.data(), body_end));
  if (cfull != stored_crc) return fail(FrameError::kChecksum);

  Message msg;
  if (!decode_body(bytes.subspan(body_start, body_len), msg)) return fail(FrameError::kMalformedBody);
  if (static_cast<std::uint32_t>(msg.type) != type) return fail(FrameError::kMalformedBody);
  msg.seq = seq;
  res.ok = true;
  res.error = FrameError::kOk;
  res.message = msg;
  return res;
}

std::optional<Message> FrameDecoder::pop() {
  if (fatal_) return std::nullopt;
  if (buffer_.size() < kFrameHeaderSize + 4u) return std::nullopt;
  util::ByteReader hr(std::span<const std::byte>(buffer_.data(), kFrameHeaderSize));
  std::uint32_t magic = 0, version = 0, type = 0, seq = 0, body_len = 0;
  if (!hr.u32(magic) || magic != kMagic) { set_fatal(); return std::nullopt; }
  if (!hr.u32(version) || version != PF_PROTOCOL_VERSION) { set_fatal(); return std::nullopt; }
  if (!hr.u32(type)) { set_fatal(); return std::nullopt; }
  if (!hr.u32(seq)) { set_fatal(); return std::nullopt; }
  if (!hr.u32(body_len)) { set_fatal(); return std::nullopt; }
  if (body_len > kMaxFrameBody) { set_fatal(); return std::nullopt; }
  const std::size_t body_start = kFrameHeaderSize;
  const std::size_t body_end = body_start + static_cast<std::size_t>(body_len);
  const std::size_t total = body_end + 4u;
  if (buffer_.size() < total) return std::nullopt;
  std::uint32_t stored_crc = 0;
  {
    util::ByteReader cr(std::span<const std::byte>(buffer_.data() + body_end, 4));
    if (!cr.u32(stored_crc)) { set_fatal(); return std::nullopt; }
  }
  std::uint32_t cfull = util::crc32(std::span<const std::byte>(buffer_.data(), body_end));
  if (cfull != stored_crc) { set_fatal(); return std::nullopt; }
  Message msg;
  if (!decode_body(std::span<const std::byte>(buffer_.data() + body_start, body_len), msg)) {
    set_fatal();
    return std::nullopt;
  }
  msg.seq = seq;
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
  return msg;
}

}  // namespace pf::proto
