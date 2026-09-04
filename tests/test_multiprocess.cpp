// Real multiprocess proof: coordinator + worker(s) as independent OS processes,
// communicating over real loopback TCP with the framed, checksummed protocol.
// Exercises the primary preempt->resume scenario, kill-before-preservation, and
// coordinator restart/recovery. No test timeouts are ever used.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "test_multiprocess currently targets Windows (Winsock + CreateProcess)"
#endif

#include "tests/test_framework.hpp"
#include "preemption_fabric/protocol/connection.hpp"

using namespace pf;
using namespace pf::proto;

// A launched OS process with a redirected stdout pipe (so we can read markers).
struct Process {
  PROCESS_INFORMATION pi{};
  HANDLE read_pipe = nullptr;
  bool ok = false;

  bool launch(const char* exe, const std::string& args) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE rpipe = nullptr, wpipe = nullptr;
    if (!CreatePipe(&rpipe, &wpipe, &sa, 0)) return false;
    SetHandleInformation(rpipe, HANDLE_FLAG_INHERIT, 0);
    char cmdline[2048];
    std::snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", exe, args.c_str());
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wpipe;
    si.hStdError = wpipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    BOOL launched = CreateProcessA(nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!launched) { CloseHandle(rpipe); CloseHandle(wpipe); return false; }
    CloseHandle(wpipe);  // child owns the write end
    read_pipe = rpipe;
    ok = true;
    return true;
  }

  std::string read_line() {
    std::string line;
    char c;
    while (true) {
      DWORD n = 0;
      if (!ReadFile(read_pipe, &c, 1, &n, nullptr)) break;
      if (n == 0) break;
      if (c == '\n') break;
      line.push_back(c);
    }
    return line;
  }

  // Read lines until one begins with 'prefix', or EOF.
  std::string wait_for(const std::string& prefix) {
    for (int i = 0; i < 1000; ++i) {
      auto line = read_line();
      if (line.empty()) return line;  // EOF
      if (line.rfind(prefix, 0) == 0) return line;
    }
    return std::string();
  }

  void terminate(DWORD code = 0) { if (pi.hProcess) TerminateProcess(pi.hProcess, code); }
  void wait() { if (pi.hProcess) { WaitForSingleObject(pi.hProcess, INFINITE); } }
  void close() {
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (read_pipe) CloseHandle(read_pipe);
    pi.hThread = nullptr; pi.hProcess = nullptr; read_pipe = nullptr;
  }
};

static bool file_exists(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return f.good();
}
static void remove_file(const std::string& p) { std::remove(p.c_str()); }

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: test_multiprocess <coordinator> <worker>\n"); return 2; }
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string coord = argv[1];
  std::string work = argv[2];
  std::string state = "multiprocess_state.bin";
  remove_file(state);

  TEST_CONTEXT("multiprocess");

  // ---------- Scenario 1: primary preempt -> resume ----------
  {
    Process pc;
    CHECK_MSG(pc.launch(coord.c_str(), "--port 0 --save " + state), "launch coordinator");
    auto portline = pc.wait_for("PORT ");
    CHECK_MSG(!portline.empty(), "coordinator port");
    int port = std::stoi(portline.substr(5));
    CHECK(port > 0);

    Process wa;
    CHECK_MSG(wa.launch(work.c_str(), std::to_string(port) + " 41 A"), "launch worker A");
    auto reg = pc.wait_for("REGISTERED ");
    CHECK_MSG(!reg.empty() && reg.find("boot=") != std::string::npos, "worker registered");

    TcpConnection controller(connect_loopback(static_cast<std::uint16_t>(port)));
    CHECK(controller.is_open());

    // Begin execution.
    Message begin; begin.type = MessageType::kBeginExecution; begin.worker_id = WorkerId(41);
    begin.worker_boot_id = WorkerBootId(1000); begin.execution_id = ExecutionId(20);
    begin.execution_generation = ExecutionGeneration(1); begin.attempt_id = AttemptId(30);
    begin.attempt_generation = AttemptGeneration(1); begin.coordinator_epoch = CoordinatorEpoch(100);
    controller.send(begin);
    auto started = pc.wait_for("EXECUTION_STARTED");
    CHECK_MSG(!started.empty(), "execution started");

    // Two segments.
    Message s1; s1.type = MessageType::kPublishProgress; s1.progress_position = 100;
    controller.send(s1);
    CHECK_MSG(!pc.wait_for("SEGMENT 100").empty(), "segment 100");

    Message s2; s2.type = MessageType::kPublishProgress; s2.progress_position = 200;
    controller.send(s2);
    CHECK_MSG(!pc.wait_for("SEGMENT 200").empty(), "segment 200");

    // Preempt.
    Message pre; pre.type = MessageType::kRequestPreemption;
    controller.send(pre);
    CHECK_MSG(!pc.wait_for("PREEMPTED 1").empty(), "preempted");
    CHECK_MSG(!pc.wait_for("DRIVE_PREEMPTION 1").empty(), "drive preemption ok");
    CHECK_MSG(file_exists(state), "state saved after preemption");

    // Resume.
    Message res; res.type = MessageType::kRequestResume;
    controller.send(res);
    CHECK_MSG(!pc.wait_for("RESUMED 1").empty(), "resumed");
    CHECK_MSG(!pc.wait_for("DRIVE_RESUME 1").empty(), "drive resume ok");

    Message shut; shut.type = MessageType::kShutdown;
    controller.send(shut);
    pc.wait();
    wa.terminate(); wa.wait(); wa.close();
    pc.close();
  }

  // ---------- Scenario 2: kill worker before durable capture ----------
  {
    Process pc;
    CHECK_MSG(pc.launch(coord.c_str(), "--port 0"), "launch coordinator 2");
    auto portline = pc.wait_for("PORT ");
    int port = std::stoi(portline.substr(5));
    CHECK(port > 0);

    Process wa;
    CHECK_MSG(wa.launch(work.c_str(), std::to_string(port) + " 42 B"), "launch worker B");
    CHECK_MSG(!pc.wait_for("REGISTERED ").empty(), "worker B registered");

    TcpConnection controller(connect_loopback(static_cast<std::uint16_t>(port)));
    Message begin; begin.type = MessageType::kBeginExecution; begin.worker_id = WorkerId(42);
    begin.worker_boot_id = WorkerBootId(1001); begin.execution_id = ExecutionId(20);
    begin.execution_generation = ExecutionGeneration(1); begin.attempt_id = AttemptId(30);
    begin.attempt_generation = AttemptGeneration(1); begin.coordinator_epoch = CoordinatorEpoch(100);
    controller.send(begin);
    CHECK_MSG(!pc.wait_for("EXECUTION_STARTED").empty(), "execution started 2");

    Message s1; s1.type = MessageType::kPublishProgress; s1.progress_position = 100;
    controller.send(s1);
    CHECK_MSG(!pc.wait_for("SEGMENT 100").empty(), "segment 100");

    // Kill worker B before the preservation boundary is crossed.
    wa.terminate(); wa.wait();

    Message pre; pre.type = MessageType::kRequestPreemption;
    controller.send(pre);
    auto dp = pc.wait_for("DRIVE_PREEMPTION ");
    // Forced worker loss must not be reported as safe preemption.
    CHECK_MSG(!dp.empty() && dp.find("DRIVE_PREEMPTION 0") != std::string::npos, "no safe preemption after worker death");

    Message shut; shut.type = MessageType::kShutdown;
    controller.send(shut);
    pc.wait();
    pc.close();
  }

  // ---------- Scenario 3: coordinator restart + recovery ----------
  {
    CHECK_MSG(file_exists(state), "state file from scenario 1");
    Process pc;
    CHECK_MSG(pc.launch(coord.c_str(), "--port 0 --recover " + state), "launch coordinator 3 (recover)");
    auto rec = pc.wait_for("RECOVER ");
    CHECK_MSG(!rec.empty() && rec.find("RECOVER 1") != std::string::npos, "recover durable state");
    auto portline = pc.wait_for("PORT ");
    int port = std::stoi(portline.substr(5));
    CHECK(port > 0);

    Process wa2;
    CHECK_MSG(wa2.launch(work.c_str(), std::to_string(port) + " 43 C"), "launch worker C (fresh boot)");
    CHECK_MSG(!pc.wait_for("REGISTERED ").empty(), "worker C registered");

    TcpConnection controller(connect_loopback(static_cast<std::uint16_t>(port)));
    Message res; res.type = MessageType::kRequestResume;
    controller.send(res);
    CHECK_MSG(!pc.wait_for("RESUMED 1").empty(), "resume after recovery");
    CHECK_MSG(!pc.wait_for("DRIVE_RESUME 1").empty(), "drive resume after recovery");

    Message shut; shut.type = MessageType::kShutdown;
    controller.send(shut);
    pc.wait();
    wa2.terminate(); wa2.wait(); wa2.close();
    pc.close();
  }

  remove_file(state);
  PF_TEST_RETURN();
}