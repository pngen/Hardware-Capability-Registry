// Hardware Capability Registry - canonical hardware capability knowledge.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "hcr/transport.hpp"

#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace hcr {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

std::once_flag g_wsa_once;
int g_wsa_result = 0;

void initialize_wsa() {
  WSADATA data{};
  g_wsa_result = WSAStartup(MAKEWORD(2, 2), &data);
}

Status last_socket_error(const char* what) {
  return Status::failure(ErrorCode::IoFailure,
                         std::string(what) + " failed with socket error " + std::to_string(WSAGetLastError()));
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

Status last_socket_error(const char* what) {
  return Status::failure(ErrorCode::IoFailure, std::string(what) + " failed");
}
#endif

SocketHandle to_handle(std::uintptr_t raw) { return static_cast<SocketHandle>(raw); }
std::uintptr_t from_handle(SocketHandle handle) { return static_cast<std::uintptr_t>(handle); }

SocketHandle from_raw(std::uintptr_t raw) { return to_handle(raw); }

void close_handle(SocketHandle handle) {
  if (handle == kInvalidSocket) return;
#if defined(_WIN32)
  ::closesocket(handle);
#else
  ::close(handle);
#endif
}

}  // namespace

Status transport_initialize() {
#if defined(_WIN32)
  std::call_once(g_wsa_once, initialize_wsa);
  if (g_wsa_result != 0) {
    return Status::failure(ErrorCode::IoFailure,
                           "Winsock initialization failed with code " + std::to_string(g_wsa_result));
  }
#endif
  return Status::success();
}

TcpConnection::~TcpConnection() { close_handle(to_handle(handle_)); }

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : handle_(other.handle_) {
  other.handle_ = kInvalidHandle;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
  if (this != &other) {
    close_handle(to_handle(handle_));
    handle_ = other.handle_;
    other.handle_ = kInvalidHandle;
  }
  return *this;
}

bool TcpConnection::valid() const noexcept { return handle_ != kInvalidHandle; }

Status TcpConnection::send_all(const std::uint8_t* data, std::size_t size) {
  if (!valid()) return Status::failure(ErrorCode::IoFailure, "send on a closed connection");
  std::size_t sent = 0;
  while (sent < size) {
    const int chunk = static_cast<int>(size - sent > 1u << 20 ? 1u << 20 : size - sent);
    const int written = ::send(to_handle(handle_), reinterpret_cast<const char*>(data + sent), chunk, 0);
    if (written <= 0) return last_socket_error("send");
    sent += static_cast<std::size_t>(written);
  }
  return Status::success();
}

Status TcpConnection::recv_some(std::uint8_t* data, std::size_t capacity, std::size_t& received) {
  received = 0;
  if (!valid()) return Status::failure(ErrorCode::IoFailure, "receive on a closed connection");
  const int chunk = static_cast<int>(capacity > 1u << 20 ? 1u << 20 : capacity);
  const int read = ::recv(to_handle(handle_), reinterpret_cast<char*>(data), chunk, 0);
  if (read == 0) return Status::success();
  if (read < 0) return last_socket_error("recv");
  received = static_cast<std::size_t>(read);
  return Status::success();
}

Status TcpConnection::available(std::size_t& bytes) const {
  bytes = 0;
  if (!valid()) return Status::failure(ErrorCode::IoFailure, "query on a closed connection");
#if defined(_WIN32)
  u_long pending = 0;
  if (::ioctlsocket(to_handle(handle_), FIONREAD, &pending) != 0) return last_socket_error("ioctlsocket");
  bytes = static_cast<std::size_t>(pending);
#else
  int pending = 0;
  if (::ioctl(to_handle(handle_), FIONREAD, &pending) != 0) return last_socket_error("ioctl");
  bytes = static_cast<std::size_t>(pending);
#endif
  return Status::success();
}

Status TcpConnection::close() {
  close_handle(to_handle(handle_));
  handle_ = kInvalidHandle;
  return Status::success();
}

std::string TcpConnection::peer() const {
  if (!valid()) return "(closed)";
  sockaddr_in address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  if (::getpeername(to_handle(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return "(unknown)";
  }
  char buffer[64];
  const char* text = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  if (text == nullptr) return "(unknown)";
  return std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
}

TcpListener::~TcpListener() { close_handle(to_handle(handle_)); }

TcpListener::TcpListener(TcpListener&& other) noexcept : handle_(other.handle_) {
  other.handle_ = kInvalidHandle;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this != &other) {
    close_handle(to_handle(handle_));
    handle_ = other.handle_;
    other.handle_ = kInvalidHandle;
  }
  return *this;
}

Status TcpListener::open_loopback(std::uint16_t port, std::size_t backlog) {
  const Status initialized = transport_initialize();
  if (!initialized.ok()) return initialized;
  close_handle(to_handle(handle_));
  handle_ = kInvalidHandle;
  const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == kInvalidSocket) return last_socket_error("socket");
  const int reuse = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    const Status status = last_socket_error("bind");
    close_handle(listener);
    return status;
  }
  if (::listen(listener, static_cast<int>(backlog > 1024 ? 1024 : backlog)) != 0) {
    const Status status = last_socket_error("listen");
    close_handle(listener);
    return status;
  }
  handle_ = from_handle(listener);
  return Status::success();
}

Status TcpListener::local_port(std::uint16_t& out) const {
  out = 0;
  if (!valid()) return Status::failure(ErrorCode::IoFailure, "listener is not open");
  sockaddr_in address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  if (::getsockname(to_handle(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return last_socket_error("getsockname");
  }
  out = ntohs(address.sin_port);
  return Status::success();
}

bool TcpListener::valid() const noexcept { return handle_ != kInvalidHandle; }

Status TcpListener::accept(TcpConnection& out) {
  if (!valid()) return Status::failure(ErrorCode::IoFailure, "listener is not open");
  const SocketHandle accepted = ::accept(to_handle(handle_), nullptr, nullptr);
  if (accepted == kInvalidSocket) return last_socket_error("accept");
  const int nodelay = 1;
  ::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
  out = TcpConnection(from_handle(accepted));
  return Status::success();
}

Status TcpListener::close() {
  close_handle(to_handle(handle_));
  handle_ = kInvalidHandle;
  return Status::success();
}

Status connect_loopback(std::uint16_t port, TcpConnection& out) {
  const Status initialized = transport_initialize();
  if (!initialized.ok()) return initialized;
  const SocketHandle connection = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (connection == kInvalidSocket) return last_socket_error("socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    const Status status = last_socket_error("connect");
    close_handle(connection);
    return status;
  }
  const int nodelay = 1;
  ::setsockopt(connection, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
  out = TcpConnection(from_handle(connection));
  return Status::success();
}

Status wait_readable(const TcpConnection* first, const TcpConnection* second, bool& first_ready,
                     bool& second_ready) {
  first_ready = false;
  second_ready = false;
  fd_set read_set;
  FD_ZERO(&read_set);
  SocketHandle maximum = 0;
  if (first != nullptr && first->valid()) {
    const SocketHandle handle = from_raw(first->native_handle());
    FD_SET(handle, &read_set);
    if (handle > maximum) maximum = handle;
  }
  if (second != nullptr && second->valid()) {
    const SocketHandle handle = from_raw(second->native_handle());
    FD_SET(handle, &read_set);
    if (handle > maximum) maximum = handle;
  }
  // A null timeout blocks until activity arrives. The registry configures no
  // timeouts anywhere: a hang is a defect, not something to paper over.
  const int ready = ::select(static_cast<int>(maximum) + 1, &read_set, nullptr, nullptr, nullptr);
  if (ready < 0) return last_socket_error("select");
  if (ready == 0) return Status::success();
  if (first != nullptr && first->valid() && FD_ISSET(from_raw(first->native_handle()), &read_set)) {
    first_ready = true;
  }
  if (second != nullptr && second->valid() && FD_ISSET(from_raw(second->native_handle()), &read_set)) {
    second_ready = true;
  }
  return Status::success();
}

Status wait_all_readable(std::uintptr_t listener, const std::vector<std::uintptr_t>& sockets,
                         bool& listener_ready, std::vector<bool>& ready) {
  listener_ready = false;
  ready.assign(sockets.size(), false);
  if (sockets.empty() && listener == 0) {
    return Status::failure(ErrorCode::InvalidArgument, "no sockets to wait on");
  }
  fd_set read_set;
  FD_ZERO(&read_set);
  SocketHandle maximum = 0;
  bool any = false;
  if (listener != 0) {
    const SocketHandle handle = from_raw(listener);
    FD_SET(handle, &read_set);
    maximum = handle;
    any = true;
  }
  for (const std::uintptr_t raw : sockets) {
    const SocketHandle handle = from_raw(raw);
    FD_SET(handle, &read_set);
    if (handle > maximum) maximum = handle;
    any = true;
  }
  if (!any) return Status::failure(ErrorCode::InvalidArgument, "no sockets to wait on");
  const int selected = ::select(static_cast<int>(maximum) + 1, &read_set, nullptr, nullptr, nullptr);
  if (selected < 0) return last_socket_error("select");
  if (selected == 0) return Status::success();
  if (listener != 0 && FD_ISSET(from_raw(listener), &read_set)) listener_ready = true;
  for (std::size_t i = 0; i < sockets.size(); ++i) {
    ready[i] = FD_ISSET(from_raw(sockets[i]), &read_set) != 0;
  }
  return Status::success();
}

}  // namespace hcr
