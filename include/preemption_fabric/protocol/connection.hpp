#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
#endif

#include "preemption_fabric/protocol/framing.hpp"

namespace pf::proto {

// A real loopback TCP connection with the framed, checksummed, bounded protocol.
// Handles partial reads/writes by looping until the full frame moves, and uses an
// internal FrameDecoder to bound buffering. Windows uses Winsock2; the abstraction
// is plain TCP in both cases.
class TcpConnection {
 public:
  explicit TcpConnection(SOCKET sock) : sock_(sock) {}
  ~TcpConnection() { close(); }
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  // Send one fully-framed message (loops over partial writes).
  bool send(const Message& message);

  // Blocking receive of the next complete frame (loops over partial reads).
  // Returns false only on connection close or a protocol violation.
  bool receive(Message& out);

  void close();
  [[nodiscard]] bool is_open() const { return sock_ != kInvalid; }
  [[nodiscard]] SOCKET raw_socket() const { return sock_; }

  // True if a fatal protocol error was observed (bad magic/version/length/crc).
  [[nodiscard]] bool protocol_failed() const { return decoder_.fatal(); }

 private:
  bool write_all(const std::byte* p, std::size_t n);
  bool read_exact(std::byte* p, std::size_t n);

  SOCKET sock_ = kInvalid;
  FrameDecoder decoder_;
  static constexpr SOCKET kInvalid = static_cast<SOCKET>(-1);
};

// Start a listening socket bound to the loopback interface on the given port.
struct Listener {
  SOCKET sock = static_cast<SOCKET>(-1);
  std::uint16_t port = 0;
};
Listener listen_loopback(std::uint16_t port = 0);

// Accept a pending connection. Returns an owned socket or invalid.
SOCKET accept_connection(SOCKET listener);

// Connect a client socket to the loopback listener. Returns an owned socket.
SOCKET connect_loopback(std::uint16_t port);

}  // namespace pf::proto
