#include "mcp/live_io.h"

#include "engine/core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace editor {
namespace {

bool EnsureWsa() {
  static bool ok = false;
  static bool tried = false;
  if (tried) {
    return ok;
  }
  tried = true;
  WSADATA wsa{};
  ok = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  return ok;
}

void SetNonBlock(SOCKET s) {
  u_long n = 1;
  ioctlsocket(s, FIONBIO, &n);
}

SOCKET AsSock(std::uintptr_t v) { return static_cast<SOCKET>(v); }

}  // namespace

bool LiveServer::Start(std::uint16_t port) {
  Stop();
  if (!EnsureWsa()) {
    return false;
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return false;
  }
  BOOL reuse = TRUE;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, kLiveHost, &addr.sin_addr);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }
  if (listen(s, 1) != 0) {
    closesocket(s);
    return false;
  }
  SetNonBlock(s);
  listen_ = static_cast<std::uintptr_t>(s);
  engine::LogInfo("editor live MCP on " + std::string(kLiveHost) + ":" + std::to_string(port));
  return true;
}

void LiveServer::Stop() {
  if (client_) {
    closesocket(AsSock(client_));
    client_ = 0;
  }
  if (listen_) {
    closesocket(AsSock(listen_));
    listen_ = 0;
  }
  buf_.clear();
}

void LiveServer::Poll(std::vector<std::string>* lines) {
  if (!listen_ || !lines) {
    return;
  }
  if (!client_) {
    SOCKET c = accept(AsSock(listen_), nullptr, nullptr);
    if (c != INVALID_SOCKET) {
      SetNonBlock(c);
      client_ = static_cast<std::uintptr_t>(c);
      engine::LogInfo("editor live MCP client connected");
    }
  }
  if (!client_) {
    return;
  }
  char tmp[2048];
  for (;;) {
    const int n = recv(AsSock(client_), tmp, static_cast<int>(sizeof(tmp)), 0);
    if (n > 0) {
      buf_.append(tmp, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0 || (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) {
      closesocket(AsSock(client_));
      client_ = 0;
      buf_.clear();
    }
    break;
  }
  std::size_t start = 0;
  for (;;) {
    const auto nl = buf_.find('\n', start);
    if (nl == std::string::npos) {
      break;
    }
    auto line = buf_.substr(start, nl - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines->push_back(std::move(line));
    }
    start = nl + 1;
  }
  if (start > 0) {
    buf_.erase(0, start);
  }
}

void LiveServer::Reply(std::string_view line) {
  if (!client_ || line.empty()) {
    return;
  }
  std::string out(line);
  out.push_back('\n');
  (void)send(AsSock(client_), out.data(), static_cast<int>(out.size()), 0);
}

bool LiveForwardStdio(std::uint16_t port) {
  if (!EnsureWsa()) {
    return false;
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, kLiveHost, &addr.sin_addr);
  DWORD ms = 250;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }
  DWORD wait = 30000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&wait), sizeof(wait));
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    line.push_back('\n');
    if (send(s, line.data(), static_cast<int>(line.size()), 0) <= 0) {
      break;
    }
    std::string acc;
    while (acc.find('\n') == std::string::npos) {
      char tmp[2048];
      const int n = recv(s, tmp, static_cast<int>(sizeof(tmp)), 0);
      if (n <= 0) {
        closesocket(s);
        return true;
      }
      acc.append(tmp, static_cast<std::size_t>(n));
    }
    auto out = acc.substr(0, acc.find('\n'));
    if (!out.empty() && out.back() == '\r') {
      out.pop_back();
    }
    std::cout << out << '\n';
    std::cout.flush();
  }
  closesocket(s);
  return true;
}

}  // namespace editor
