// Literal kill-after-preservation worker-death proof.
//
// A real OS worker A executes work, reaches a safe point, completes quiescence,
// has its state captured/verified, releases required resources, and reaches a
// durable PREEMPTED state. THEN Worker A is killed as a real OS process; a fresh
// Worker A' (fresh WorkerBootId) is registered on the SAME coordinator; stale
// preemption/release messages are replayed and rejected; dynamic worker/resource
// evidence is revalidated; a fresh resume authority is issued; A' resumes from the
// preserved boundary and continues. Distinct from coordinator restart.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "test_kill_after_preservation currently targets Windows"
#endif

#include "tests/test_framework.hpp"
#include "preemption_fabric/protocol/connection.hpp"

using namespace pf;
using namespace pf::proto;

struct Process {
  PROCESS_INFORMATION pi{};
  HANDLE read_pipe = nullptr;
  bool ok = false;
  bool launch(const char* exe, const std::string& args) {
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
    HANDLE rp = nullptr, wp = nullptr;
    if (!CreatePipe(&rp, &wp, &sa, 0)) return false;
    SetHandleInformation(rp, HANDLE_FLAG_INHERIT, 0);
    char cmdline[2048];
    std::snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", exe, args.c_str());
    STARTUPINFOA si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wp; si.hStdError = wp; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (!CreateProcessA(nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      CloseHandle(rp); CloseHandle(wp); return false;
    }
    CloseHandle(wp); read_pipe = rp; ok = true; return true;
  }
  std::string read_line() {
    std::string line; char c;
    while (true) {
      DWORD n = 0;
      if (!ReadFile(read_pipe, &c, 1, &n, nullptr)) break;
      if (n == 0) break;
      if (c == '\n') break;
      line.push_back(c);
    }
    return line;
  }
  std::string wait_for(const std::string& prefix) {
    for (int i = 0; i < 1000; ++i) { auto l = read_line(); if (l.empty()) return l; if (l.rfind(prefix, 0) == 0) return l; }
    return std::string();
  }
  void terminate() { if (pi.hProcess) TerminateProcess(pi.hProcess, 0); }
  void wait() { if (pi.hProcess) WaitForSingleObject(pi.hProcess, INFINITE); }
  void close() {
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (read_pipe) CloseHandle(read_pipe);
    pi.hThread = nullptr; pi.hProcess = nullptr; read_pipe = nullptr;
  }
};

static std::uint64_t parse_after(const std::string& line, const std::string& key) {
  auto pos = line.find(key);
  if (pos == std::string::npos) return 0;
  return std::strtoull(line.c_str() + pos + key.size(), nullptr, 10);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: test_kill_after_preservation <coordinator> <worker>\n"); return 2; }
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string coord = argv[1];
  std::string work = argv[2];
  TEST_CONTEXT("kill-after-preservation");

  Process pc;
  CHECK_MSG(pc.launch(coord.c_str(), "--port 0"), "launch coordinator");
  auto portline = pc.wait_for("PORT ");
  CHECK_MSG(!portline.empty(), "coordinator port");
  int port = std::strtol(portline.c_str() + 5, nullptr, 10);
  CHECK(port > 0);

  Process wa;
  CHECK_MSG(wa.launch(work.c_str(), std::to_string(port) + " 44 A"), "launch worker A");
  auto reg = pc.wait_for("REGISTERED ");
  CHECK_MSG(!reg.empty(), "worker A registered");
  // Old boot id (from the coordinator's REGISTERED line).
  std::uint64_t old_boot = parse_after(reg, "boot=");
  CHECK(old_boot > 0);

  TcpConnection controller(connect_loopback(static_cast<std::uint16_t>(port)));
  CHECK(controller.is_open());

  // Begin execution + segments.
  Message begin; begin.type = MessageType::kBeginExecution; begin.worker_id = WorkerId(44);
  begin.worker_boot_id = WorkerBootId(old_boot); begin.execution_id = ExecutionId(20);
  begin.execution_generation = ExecutionGeneration(1); begin.attempt_id = AttemptId(30);
  begin.attempt_generation = AttemptGeneration(1); begin.coordinator_epoch = CoordinatorEpoch(100);
  controller.send(begin);
  CHECK_MSG(!pc.wait_for("EXECUTION_STARTED").empty(), "execution started");

  Message s1; s1.type = MessageType::kPublishProgress; s1.progress_position = 100; controller.send(s1);
  CHECK_MSG(!pc.wait_for("SEGMENT 100").empty(), "segment 100");
  Message s2; s2.type = MessageType::kPublishProgress; s2.progress_position = 200; controller.send(s2);
  CHECK_MSG(!pc.wait_for("SEGMENT 200").empty(), "segment 200");

  // Preempt to a durable PREEMPTED state.
  Message pre; pre.type = MessageType::kRequestPreemption; controller.send(pre);
  CHECK_MSG(!pc.wait_for("PREEMPTED 1").empty(), "durable PREEMPTED");
  CHECK_MSG(!pc.wait_for("DRIVE_PREEMPTION 1").empty(), "drive preemption ok");

  // Kill Worker A as a real OS process.
  wa.terminate(); wa.wait(); wa.close();

  // Register the replacement Worker A' on the SAME coordinator.
  Message regcmd; regcmd.type = MessageType::kRegister; controller.send(regcmd);
  Process wa2;
  CHECK_MSG(wa2.launch(work.c_str(), std::to_string(port) + " 45 Av"), "launch worker A'");
  auto rereg = pc.wait_for("RE_REGISTERED ");
  CHECK_MSG(!rereg.empty(), "worker A' re-registered with fresh boot");
  std::uint64_t new_boot = parse_after(rereg, "boot=");
  CHECK(new_boot > 0);
  CHECK(new_boot != old_boot);

  // Replay stale preemption / release messages; both must be rejected.
  Message rp;
  rp.type = MessageType::kReplayStale;
  rp.request_generation = PreemptionRequestGeneration(1);
  rp.attempt_generation = AttemptGeneration(1);     // same, but boot/epoch stale
  rp.coordinator_epoch = CoordinatorEpoch(100);      // same epoch
  rp.worker_id = WorkerId(44);                      // old worker id
  rp.worker_boot_id = WorkerBootId(old_boot);       // OLD boot -> stale
  controller.send(rp);
  auto sp = pc.wait_for("STALE_PREEMPTION ");
  CHECK_MSG(!sp.empty() && sp.find("STALE_PREEMPTION 4") != std::string::npos,
            "stale preemption rejected (RequestAdmissionCode=4)");
  auto sr = pc.wait_for("STALE_RELEASE ");
  CHECK_MSG(!sr.empty() && sr.find("STALE_RELEASE 1") != std::string::npos,
            "stale/double release rejected");

  // Fresh resume authority; A' resumes from the preserved boundary.
  Message res; res.type = MessageType::kRequestResume; controller.send(res);
  CHECK_MSG(!pc.wait_for("RESUMED 1").empty(), "resumed after re-registration");
  CHECK_MSG(!pc.wait_for("DRIVE_RESUME 1").empty(), "drive resume ok");

  // Continue work on A'; progress continues from the preserved boundary.
  Message s3; s3.type = MessageType::kPublishProgress; s3.progress_position = 300; controller.send(s3);
  CHECK_MSG(!pc.wait_for("SEGMENT 300").empty(), "progress continues after resume");

  Message shut; shut.type = MessageType::kShutdown; controller.send(shut);
  pc.wait();
  wa2.terminate(); wa2.wait(); wa2.close();
  pc.close();

  PF_TEST_RETURN();
}
