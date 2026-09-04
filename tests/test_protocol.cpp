#include <vector>
#include "tests/test_framework.hpp"
#include "preemption_fabric/protocol/framing.hpp"
#include "preemption_fabric/version.hpp"

using namespace pf;
using namespace pf::proto;

int main() {
  TEST_CONTEXT("protocol");

  Message m;
  m.type = MessageType::kPublishProgress;
  m.seq = 3;
  m.coordinator_epoch = CoordinatorEpoch(1);
  m.worker_id = WorkerId(40);
  m.worker_boot_id = WorkerBootId(50);
  m.execution_id = ExecutionId(20);
  m.execution_generation = ExecutionGeneration(1);
  m.attempt_id = AttemptId(30);
  m.attempt_generation = AttemptGeneration(1);
  m.progress_position = 42;
  m.progress_kind = ProgressKind::kDurable;
  m.detail = "hello";

  auto frame = encode_frame(m);
  auto res = decode_frame(frame);
  CHECK(res.ok);
  CHECK(res.error == FrameError::kOk);
  CHECK(res.message.type == MessageType::kPublishProgress);
  CHECK_EQ(res.message.seq, 3);
  CHECK_EQ(res.message.progress_position, 42);
  CHECK_EQ(res.message.detail, "hello");

  // Truncated frame.
  auto trunc = std::vector<std::byte>(frame.begin(), frame.begin() + frame.size() - 2);
  auto r2 = decode_frame(trunc);
  CHECK(!r2.ok);
  CHECK(r2.error == FrameError::kTruncated);

  // Trailing garbage.
  auto trail = frame;
  trail.push_back(static_cast<std::byte>(0x01));
  auto r3 = decode_frame(trail);
  CHECK(!r3.ok);
  CHECK(r3.error == FrameError::kTrailingGarbage);

  // Oversized declared length.
  auto big = frame;
  big[16] = static_cast<std::byte>(0xFF);  // body_len byte -> huge
  big[17] = static_cast<std::byte>(0xFF);
  big[18] = static_cast<std::byte>(0xFF);
  big[19] = static_cast<std::byte>(0x7F);
  auto r4 = decode_frame(big);
  CHECK(!r4.ok);
  CHECK(r4.error == FrameError::kBadLength);

  // Checksum corruption.
  auto corr = frame;
  corr[24] = static_cast<std::byte>(std::to_integer<unsigned char>(corr[24]) ^ 0x55);
  auto r5 = decode_frame(corr);
  CHECK(!r5.ok);
  CHECK(r5.error == FrameError::kChecksum);

  // Bad magic.
  auto bm = frame;
  bm[0] = static_cast<std::byte>('Q');
  auto r6 = decode_frame(bm);
  CHECK(!r6.ok);
  CHECK(r6.error == FrameError::kBadMagic);

  // Streaming decoder with partial feeds.
  FrameDecoder dec;
  // Feed one byte at a time in reverse order to exercise partial reads.
  bool got = false;
  Message out;
  for (size_t i = 0; i < frame.size(); ++i) {
    dec.feed(std::span<const std::byte>(frame.data() + i, 1));
    auto popped = dec.pop();
    if (popped) { got = true; out = *popped; }
  }
  CHECK(got);
  CHECK(out.type == MessageType::kPublishProgress);
  CHECK_EQ(out.progress_position, 42);

  PF_TEST_RETURN();
}
