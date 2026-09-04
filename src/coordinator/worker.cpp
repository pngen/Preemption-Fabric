// Reference Worker process for the real multiprocess proof.
//
// A worker is an independent OS process that registers with the coordinator over
// real loopback TCP, executes segmented work, publishes progress and safe points,
// and participates in quiescence/resume. It uses the framed, checksummed protocol
// and never shares memory with the coordinator.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "preemption_fabric/protocol/connection.hpp"

using namespace pf;
using namespace pf::proto;

static std::uint16_t parse_port(const char* s) {
  return static_cast<std::uint16_t>(std::atoi(s));
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: worker <port> <worker_id> <label>\n");
    return 2;
  }
  std::uint16_t port = parse_port(argv[1]);
  WorkerId worker_id(static_cast<std::uint64_t>(std::atoll(argv[2])));
  std::string label = argv[3];

  SOCKET s = connect_loopback(port);
  if (s == static_cast<SOCKET>(-1)) {
    std::fprintf(stderr, "worker[%s]: connect failed\n", label.c_str());
    return 1;
  }
  TcpConnection conn(s);
  std::uint64_t seq = 0;

  // HELLO / REGISTER
  Message hello;
  hello.type = MessageType::kHello;
  hello.seq = static_cast<std::uint32_t>(seq++);
  hello.worker_id = worker_id;
  hello.detail = label;
  if (!conn.send(hello)) { std::fprintf(stderr, "worker[%s]: hello send failed\n", label.c_str()); return 1; }

  Message reg;
  reg.type = MessageType::kRegister;
  reg.seq = static_cast<std::uint32_t>(seq++);
  reg.worker_id = worker_id;
  reg.detail = label;
  if (!conn.send(reg)) { std::fprintf(stderr, "worker[%s]: register send failed\n", label.c_str()); return 1; }

  // Await REGISTER acknowledgment with a fresh WorkerBootId.
  Message reg_ack;
  if (!conn.receive(reg_ack)) { std::fprintf(stderr, "worker[%s]: register ack failed\n", label.c_str()); return 1; }
  WorkerBootId boot = reg_ack.worker_boot_id;
  CoordinatorEpoch epoch = reg_ack.coordinator_epoch;
  std::printf("worker[%s] registered boot=%llu epoch=%llu\n", label.c_str(),
              (unsigned long long)boot.value(), (unsigned long long)epoch.value());
  std::fflush(stdout);

  // Command loop.
  for (;;) {
    Message cmd;
    if (!conn.receive(cmd)) {
      std::fprintf(stderr, "worker[%s]: coordinator closed\n", label.c_str());
      return 0;
    }
    switch (cmd.type) {
      case MessageType::kBeginExecution: {
        // Record the execution context; no acknowledgement (the coordinator
        // proceeds to issue segment commands).
        break;
      }
      case MessageType::kPublishProgress: {
        // Segment work: publish progress and reach the next safe point.
        Message prog;
        prog.type = MessageType::kPublishProgress;
        prog.seq = static_cast<std::uint32_t>(seq++);
        prog.worker_id = worker_id;
        prog.worker_boot_id = boot;
        prog.execution_id = cmd.execution_id;
        prog.execution_generation = cmd.execution_generation;
        prog.attempt_id = cmd.attempt_id;
        prog.attempt_generation = cmd.attempt_generation;
        prog.coordinator_epoch = epoch;
        prog.progress_position = cmd.progress_position;
        prog.progress_kind = ProgressKind::kDurable;
        conn.send(prog);

        Message reach;
        reach.type = MessageType::kReachSafePoint;
        reach.seq = static_cast<std::uint32_t>(seq++);
        reach.worker_id = worker_id;
        reach.worker_boot_id = boot;
        reach.execution_id = cmd.execution_id;
        reach.execution_generation = cmd.execution_generation;
        reach.attempt_id = cmd.attempt_id;
        reach.attempt_generation = cmd.attempt_generation;
        reach.coordinator_epoch = epoch;
        reach.safe_point_id = cmd.safe_point_id;
        reach.safe_point_position = cmd.progress_position;
        reach.safe_point_capture_required = cmd.safe_point_capture_required;
        conn.send(reach);
        break;
      }
      case MessageType::kQuiesced: {
        // Acknowledge quiescence readiness.
        Message ack;
        ack.type = MessageType::kQuiesced;
        ack.seq = static_cast<std::uint32_t>(seq++);
        ack.worker_id = worker_id;
        ack.worker_boot_id = boot;
        ack.coordinator_epoch = epoch;
        ack.execution_id = cmd.execution_id;
        ack.execution_generation = cmd.execution_generation;
        ack.attempt_id = cmd.attempt_id;
        ack.attempt_generation = cmd.attempt_generation;
        conn.send(ack);
        break;
      }
      case MessageType::kResumed: {
        Message ack;
        ack.type = MessageType::kResumed;
        ack.seq = static_cast<std::uint32_t>(seq++);
        ack.worker_id = worker_id;
        ack.worker_boot_id = boot;
        ack.coordinator_epoch = epoch;
        ack.execution_id = cmd.execution_id;
        ack.execution_generation = cmd.execution_generation;
        ack.attempt_id = cmd.attempt_id;
        ack.attempt_generation = cmd.attempt_generation;
        ack.resume_generation = cmd.resume_generation;
        conn.send(ack);
        break;
      }
      case MessageType::kShutdown: {
        Message ack;
        ack.type = MessageType::kShutdown;
        ack.seq = static_cast<std::uint32_t>(seq++);
        conn.send(ack);
        std::printf("worker[%s] shutdown\n", label.c_str());
        std::fflush(stdout);
        return 0;
      }
      default:
        std::fprintf(stderr, "worker[%s]: unexpected type %u\n", label.c_str(),
                     static_cast<unsigned>(cmd.type));
        return 1;
    }
  }
}
