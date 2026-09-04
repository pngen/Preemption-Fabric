// Reference Coordinator process for the real multiprocess proof.
//
// The coordinator owns the PreemptionRuntime plus the narrow reference adapters
// (the "owning runtimes"). It accepts a real worker connection and a controller
// connection over loopback TCP, then drives the scenario. Workers are real
// processes; every message is a framed, checksummed frame over a real socket.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "preemption_fabric/adapters/reference.hpp"
#include "preemption_fabric/protocol/connection.hpp"
#include "preemption_fabric/runtime.hpp"
#include "preemption_fabric/version.hpp"

using namespace pf;
using namespace pf::proto;

namespace {
std::uint16_t parse_port(const char* s) { return static_cast<std::uint16_t>(std::atoi(s)); }
uint64_t parse_u64(const char* s) { return std::strtoull(s, nullptr, 10); }
}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 0;
  std::string recover;
  std::string save;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = parse_port(argv[++i]);
    else if (std::strcmp(argv[i], "--recover") == 0 && i + 1 < argc) recover = argv[++i];
    else if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = argv[++i];
  }

  adapter::ReferenceAdapters ad;
  for (auto c : ad.categories()) ad.set_quiesced(c, true);
  ad.set_contract(ExecutionId(20), ResourceContractGeneration(1), {{ResourceClass::kDeviceMemory, 100}});
  ad.set_resume_dependency(adapter::DependencyStatus::kCurrent, DependencyGeneration(1), false, false);

  PreemptionRuntime rt(CoordinatorEpoch(100), PolicyGeneration(1), ad);
  if (!recover.empty()) { std::printf("RECOVER %d\n", rt.load(recover) ? 1 : 0); std::fflush(stdout); }

  auto listener = listen_loopback(port);
  if (listener.sock == static_cast<SOCKET>(-1)) { std::fprintf(stderr, "coordinator: listen failed\n"); return 1; }
  std::printf("PORT %u\n", static_cast<unsigned>(listener.port));
  std::fflush(stdout);

  // Accept the worker->
  SOCKET ws = accept_connection(listener.sock);
  if (ws == static_cast<SOCKET>(-1)) return 1;
  std::unique_ptr<TcpConnection> worker(new TcpConnection(ws));
  Message hello, reg;
  if (!worker->receive(hello) || !worker->receive(reg) || reg.type != MessageType::kRegister) {
    std::fprintf(stderr, "coordinator: bad worker registration\n");
    return 1;
  }
  WorkerId worker_id = reg.worker_id;
  WorkerBootId boot(static_cast<std::uint64_t>(1000) +
                    static_cast<std::uint64_t>(std::hash<std::string>{}(std::string(reg.detail)) % 9000));
  rt.set_worker_incarnation(worker_id, boot);
  Message reg_ack;
  reg_ack.type = MessageType::kRegister;
  reg_ack.worker_id = worker_id;
  reg_ack.worker_boot_id = boot;
  reg_ack.coordinator_epoch = CoordinatorEpoch(100);
  worker->send(reg_ack);
  std::printf("REGISTERED worker=%llu boot=%llu\n", (unsigned long long)worker_id.value(), (unsigned long long)boot.value());
  std::fflush(stdout);

  // Accept the controller connection.
  SOCKET cs = accept_connection(listener.sock);
  if (cs == static_cast<SOCKET>(-1)) return 1;
  TcpConnection controller(cs);

  ExecutionId exec_id(20);
  ExecutionGeneration exec_gen(1);
  AttemptId attempt_id(30);
  AttemptGeneration attempt_gen(1);
  SafePointId safe_point(80);
  bool started = false;
  std::uint64_t last_progress = 0;
  std::uint64_t boot_counter = 0;

  auto run_segment = [&](std::uint64_t target) -> bool {
    if (!worker->is_open()) return false;
    Message cmd;
    cmd.type = MessageType::kPublishProgress;
    cmd.worker_id = worker_id;
    cmd.worker_boot_id = boot;
    cmd.execution_id = exec_id;
    cmd.execution_generation = exec_gen;
    cmd.attempt_id = attempt_id;
    cmd.attempt_generation = attempt_gen;
    cmd.coordinator_epoch = CoordinatorEpoch(100);
    cmd.safe_point_id = safe_point;
    cmd.progress_position = target;
    cmd.safe_point_capture_required = true;
    if (!worker->send(cmd)) { std::printf("WORKER_LOST\n"); std::fflush(stdout); return false; }
    Message prog, reach;
    if (!worker->receive(prog)) { std::printf("WORKER_LOST\n"); std::fflush(stdout); return false; }
    if (!worker->receive(reach)) { std::printf("WORKER_LOST\n"); std::fflush(stdout); return false; }
    rt.reach_safe_point(safe_point, reach.safe_point_position, false);
    rt.publish_progress(prog.progress_position, prog.progress_kind);
    last_progress = prog.progress_position;
    return true;
  };

  auto drive_preemption = [&]() -> bool {
    if (!worker->is_open()) return false;
    // Admit a fresh, valid preemption request.
    PreemptionRequest req;
    req.id = PreemptionRequestId(1000);
    req.generation = PreemptionRequestGeneration(1);
    req.workload_id = WorkloadId(10); req.workload_generation = WorkloadGeneration(1);
    req.execution_id = exec_id; req.execution_generation = exec_gen;
    req.attempt_id = attempt_id; req.attempt_generation = attempt_gen;
    req.coordinator_epoch = CoordinatorEpoch(100);
    req.worker_id = worker_id; req.worker_boot_id = boot;
    req.reason = PreemptionReason::kCapacityPressure;
    req.policy_generation = PolicyGeneration(1);
    req.authority_generation = AuthorityGeneration(1);
    rt.request_preemption(req);
    rt.begin_quiesce();            // defer (work between boundaries) -> waiting
    rt.reach_safe_point(safe_point, last_progress, true);  // reach next boundary
    rt.begin_quiesce();            // now quiescing
    Message q;
    q.type = MessageType::kQuiesced;
    q.worker_id = worker_id; q.worker_boot_id = boot; q.execution_id = exec_id; q.execution_generation = exec_gen;
    q.attempt_id = attempt_id; q.attempt_generation = attempt_gen; q.coordinator_epoch = CoordinatorEpoch(100);
    if (!worker->send(q)) { std::printf("WORKER_LOST\n"); std::fflush(stdout); return false; }
    Message qack;
    if (!worker->receive(qack)) { std::printf("WORKER_LOST\n"); std::fflush(stdout); return false; }
    QuiescenceReport report;
    report.generation = QuiescenceGeneration(1);
    report.worker_id = worker_id; report.worker_boot_id = boot; report.fully_quiesced = true;
    report.provenance = EvidenceKind::kReal;
    rt.report_quiesced(report);
    rt.request_state_capture();
    if (ad.last_capture()) rt.report_state_captured(*ad.last_capture());
    rt.request_release();
    rt.report_released();
    auto cp = rt.commit_preempted();
    if (!save.empty() && cp.outcome == PreemptionOutcome::kSafePreemptionCompleted) rt.save(save);
    std::printf("PREEMPTED %d lifecycle=%s\n", cp.outcome == PreemptionOutcome::kSafePreemptionCompleted ? 1 : 0,
                std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
    std::fflush(stdout);
    return cp.outcome == PreemptionOutcome::kSafePreemptionCompleted;
  };

  auto drive_resume = [&]() -> bool {
    auto el = rt.request_resume();
    auto rv = rt.revalidate();
    auto rs = rt.restore();
    auto rm = rt.confirm_resumed();
    std::printf("RESUMED %d lifecycle=%s\n", rm.outcome == PreemptionOutcome::kSafePreemptionCompleted ? 1 : 0,
                std::string(std::string_view(to_string(rt.lifecycle()))).c_str());
    std::fflush(stdout);
    (void)el; (void)rv; (void)rs;
    return rm.outcome == PreemptionOutcome::kSafePreemptionCompleted;
  };

  for (;;) {
    Message cmd;
    if (!controller.receive(cmd)) break;
    if (cmd.type == MessageType::kShutdown) break;
    if (cmd.type == MessageType::kBeginExecution) {
      started = true;
      rt.start_execution(WorkloadId(10), WorkloadGeneration(1), exec_id, exec_gen, attempt_id, attempt_gen,
                         WorkerId(40), boot);
      ad.set_authoritative(exec_id, attempt_id, exec_gen, attempt_gen, CoordinatorEpoch(100), boot);
      SafePoint sp;
      sp.id = safe_point;
      sp.generation = SafePointGeneration(1);
      sp.execution_id = exec_id;
      sp.attempt_id = attempt_id;
      sp.worker_id = worker_id;
      sp.worker_boot_id = boot;
      sp.state_capture_required = false;
      sp.checkpoint_required = true;
      sp.resume_requirement = true;
      sp.invariant = "reference boundary";
      sp.provenance = EvidenceKind::kReal;
      sp.state = SafePointState::kDeclared;
      rt.declare_safe_point(sp);
      Message b;
      b.type = MessageType::kBeginExecution;
      b.execution_id = exec_id; b.execution_generation = exec_gen; b.attempt_id = attempt_id;
      b.attempt_generation = attempt_gen; b.coordinator_epoch = CoordinatorEpoch(100); b.safe_point_id = safe_point;
      b.worker_boot_id = boot;
      worker->send(b);
      std::printf("EXECUTION_STARTED\n");
      std::fflush(stdout);
    } else if (cmd.type == MessageType::kPublishProgress) {
      if (run_segment(cmd.progress_position)) { std::printf("SEGMENT %llu\n", (unsigned long long)cmd.progress_position); std::fflush(stdout); }
    } else if (cmd.type == MessageType::kRequestPreemption) {
      bool ok = drive_preemption();
      std::printf("DRIVE_PREEMPTION %d\n", ok ? 1 : 0);
      std::fflush(stdout);
    } else if (cmd.type == MessageType::kRequestResume) {
      bool ok = drive_resume();
      std::printf("DRIVE_RESUME %d\n", ok ? 1 : 0);
      std::fflush(stdout);
    } else if (cmd.type == MessageType::kRegister) {
      SOCKET ns = accept_connection(listener.sock);
      if (ns == static_cast<SOCKET>(-1)) { std::printf("RE_REGISTER_FAILED\n"); std::fflush(stdout); continue; }
      auto nw = std::make_unique<TcpConnection>(ns);
      Message nh, nr;
      if (!nw->receive(nh) || !nw->receive(nr) || nr.type != MessageType::kRegister) {
        std::printf("RE_REGISTER_BAD\n"); std::fflush(stdout); continue;
      }
      worker_id = nr.worker_id;
      boot = WorkerBootId(8000 + (++boot_counter));
      rt.set_worker_incarnation(worker_id, boot);
      Message ack;
      ack.type = MessageType::kRegister;
      ack.worker_id = worker_id;
      ack.worker_boot_id = boot;
      ack.coordinator_epoch = CoordinatorEpoch(100);
      nw->send(ack);
      worker = std::move(nw);
      std::printf("RE_REGISTERED worker=%llu boot=%llu\n", (unsigned long long)worker_id.value(),
                  (unsigned long long)boot.value());
      std::fflush(stdout);
    } else if (cmd.type == MessageType::kReplayStale) {
      PreemptionRequest stale;
      stale.id = PreemptionRequestId(3000);
      stale.generation = cmd.request_generation;
      stale.workload_id = WorkloadId(10);
      stale.workload_generation = WorkloadGeneration(1);
      stale.execution_id = exec_id;
      stale.execution_generation = exec_gen;
      stale.attempt_id = attempt_id;
      stale.attempt_generation = cmd.attempt_generation;
      stale.coordinator_epoch = cmd.coordinator_epoch;
      stale.worker_id = cmd.worker_id;
      stale.worker_boot_id = cmd.worker_boot_id;
      stale.policy_generation = PolicyGeneration(1);
      stale.authority_generation = AuthorityGeneration(1);
      auto s = rt.request_preemption(stale);
      std::printf("STALE_PREEMPTION %d\n", (int)s.code);
      auto rel = rt.request_release();
      std::printf("STALE_RELEASE %d\n", (int)(rel.outcome != PreemptionOutcome::kSafePreemptionCompleted));
      std::fflush(stdout);
    }
  }

  worker->close();
  controller.close();
#ifdef _WIN32
  ::closesocket(listener.sock);
#else
  ::close(listener.sock);
#endif
  return 0;
}
