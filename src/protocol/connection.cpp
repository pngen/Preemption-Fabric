#include "preemption_fabric/protocol/connection.hpp"

#include <cstddef>
#include <vector>

namespace pf::proto {
namespace {

#ifdef _WIN32
bool ensure_winsock() {
  static bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}
#else
bool ensure_winsock() { return true; }
#endif

bool set_reuse(SOCKET s) {
#ifdef _WIN32
  BOOL on = TRUE;
  return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on)) == 0;
#else
  int on = 1;
  return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0;
#endif
}

void close_socket(SOCKET s) {
  if (s != static_cast<SOCKET>(-1)) {
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
  }
}

}  // namespace

Listener listen_loopback(std::uint16_t port) {
  Listener l;
  if (!ensure_winsock()) return l;
  SOCKET s = static_cast<SOCKET>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (s == static_cast<SOCKET>(-1)) return l;
  set_reuse(s);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_socket(s); return l; }
  if (::listen(s, 16) != 0) { close_socket(s); return l; }
  if (port == 0) {
    sockaddr_in actual{};
#ifdef _WIN32
    int len = sizeof(actual);
#else
    socklen_t len = sizeof(actual);
#endif
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&actual), &len) == 0) l.port = ntohs(actual.sin_port);
  } else {
    l.port = port;
  }
  l.sock = s;
  return l;
}

SOCKET accept_connection(SOCKET listener) {
  sockaddr_in peer{};
#ifdef _WIN32
  int len = sizeof(peer);
#else
  socklen_t len = sizeof(peer);
#endif
  return ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
}

SOCKET connect_loopback(std::uint16_t port) {
  if (!ensure_winsock()) return static_cast<SOCKET>(-1);
  SOCKET s = static_cast<SOCKET>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (s == static_cast<SOCKET>(-1)) return s;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_socket(s); return static_cast<SOCKET>(-1); }
  return s;
}

bool TcpConnection::write_all(const std::byte* p, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    std::size_t remaining = n - sent;
    int chunk = static_cast<int>(remaining > 1048576u ? 1048576u : remaining);
    int r = static_cast<int>(::send(sock_, reinterpret_cast<const char*>(p + sent), chunk, 0));
    if (r <= 0) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool TcpConnection::read_exact(std::byte* p, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    std::size_t remaining = n - got;
    int chunk = static_cast<int>(remaining > 1048576u ? 1048576u : remaining);
    int r = static_cast<int>(::recv(sock_, reinterpret_cast<char*>(p + got), chunk, 0));
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool TcpConnection::send(const Message& message) {
  if (sock_ == kInvalid) return false;
  auto frame = encode_frame(message);
  return write_all(frame.data(), frame.size());
}

bool TcpConnection::receive(Message& out) {
  if (sock_ == kInvalid) return false;
  std::vector<std::byte> buf(4096);
  for (;;) {
    if (decoder_.fatal()) return false;
    if (auto popped = decoder_.pop()) { out = *popped; return true; }
    int r = static_cast<int>(::recv(sock_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0));
    if (r == 0) return false;
    if (r < 0) return false;
    decoder_.feed(std::span<const std::byte>(buf.data(), static_cast<std::size_t>(r)));
  }
}

void TcpConnection::close() {
  close_socket(sock_);
  sock_ = kInvalid;
}

}  // namespace pf::proto
