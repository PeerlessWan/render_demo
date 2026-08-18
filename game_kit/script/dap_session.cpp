#include "game_kit/dap.h"

#include <cctype>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace game_kit {
namespace {

std::string FindString(std::string_view json, std::string_view key) {
  const std::string pat = std::string("\"") + std::string(key) + "\"";
  auto at = json.find(pat);
  if (at == std::string_view::npos) {
    return {};
  }
  at = json.find(':', at + pat.size());
  if (at == std::string_view::npos) {
    return {};
  }
  ++at;
  while (at < json.size() && std::isspace(static_cast<unsigned char>(json[at]))) {
    ++at;
  }
  if (at >= json.size() || json[at] != '"') {
    return {};
  }
  ++at;
  std::string o;
  while (at < json.size() && json[at] != '"') {
    if (json[at] == '\\' && at + 1 < json.size()) {
      o.push_back(json[at + 1]);
      at += 2;
      continue;
    }
    o.push_back(json[at]);
    ++at;
  }
  return o;
}

int FindInt(std::string_view json, std::string_view key, int fallback = 0) {
  const std::string pat = std::string("\"") + std::string(key) + "\"";
  auto at = json.find(pat);
  if (at == std::string_view::npos) {
    return fallback;
  }
  at = json.find(':', at + pat.size());
  if (at == std::string_view::npos) {
    return fallback;
  }
  return std::atoi(std::string(json.substr(at + 1, 16)).c_str());
}

std::string Escape(std::string_view s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
    }
    if (c == '\n') {
      o += "\\n";
      continue;
    }
    o.push_back(c);
  }
  return o;
}

}  // namespace

void DapSession::ApplyBreakpoints(std::string_view json) {
  if (!dbg_) {
    return;
  }
  const auto path = FindString(json, "path");
  std::size_t search = 0;
  while (search < json.size()) {
    auto at = json.find("\"line\"", search);
    if (at == std::string_view::npos) {
      break;
    }
    at = json.find(':', at);
    if (at == std::string_view::npos) {
      break;
    }
    const int line = std::atoi(std::string(json.substr(at + 1, 12)).c_str());
    if (line > 0) {
      dbg_->AddBreakpoint(path, line);
    }
    search = at + 1;
  }
}

std::string DapSession::Dispatch(std::string_view json) {
  const auto cmd = FindString(json, "command");
  const int req = FindInt(json, "seq", FindInt(json, "request_seq", seq_));
  auto reply = [&](std::string_view body) {
    std::ostringstream o;
    o << "{\"seq\":" << ++seq_ << ",\"type\":\"response\",\"request_seq\":" << req
      << ",\"success\":true,\"command\":\"" << Escape(cmd) << "\",\"body\":" << body << "}";
    return o.str();
  };
  if (cmd == "initialize") {
    return reply("{\"supportsConfigurationDoneRequest\":true}");
  }
  if (cmd == "setBreakpoints") {
    ApplyBreakpoints(json);
    return reply("{\"breakpoints\":[{\"verified\":true}]}");
  }
  if (cmd == "configurationDone" || cmd == "launch" || cmd == "attach" || cmd == "threads") {
    if (cmd == "threads") {
      return reply("{\"threads\":[{\"id\":1,\"name\":\"main\"}]}");
    }
    return reply("{}");
  }
  if (cmd == "continue") {
    if (dbg_) {
      dbg_->Continue();
    }
    return reply("{\"allThreadsContinued\":true}");
  }
  if (cmd == "next") {
    if (dbg_) {
      dbg_->Step();
      dbg_->Continue();
    }
    return reply("{}");
  }
  if (cmd == "stackTrace" && dbg_) {
    std::ostringstream body;
    body << "{\"stackFrames\":[";
    const auto& st = dbg_->stack();
    if (st.empty()) {
      body << "{\"id\":0,\"name\":\"chunk\",\"line\":" << dbg_->line() << ",\"column\":1,\"source\":{\"path\":\""
           << Escape(dbg_->chunk()) << "\"}}";
    } else {
      for (std::size_t i = 0; i < st.size(); ++i) {
        if (i) {
          body << ',';
        }
        body << "{\"id\":" << i << ",\"name\":\"" << Escape(st[i].name) << "\",\"line\":" << st[i].line
             << ",\"column\":1,\"source\":{\"path\":\"" << Escape(dbg_->chunk()) << "\"}}";
      }
    }
    body << "],\"totalFrames\":" << (st.empty() ? 1 : static_cast<int>(st.size())) << "}";
    return reply(body.str());
  }
  if (cmd == "scopes") {
    return reply("{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":1,\"expensive\":false}]}");
  }
  if (cmd == "variables" && dbg_) {
    std::ostringstream body;
    body << "{\"variables\":[";
    const auto& loc = dbg_->locals();
    for (std::size_t i = 0; i < loc.size(); ++i) {
      if (i) {
        body << ',';
      }
      body << "{\"name\":\"" << Escape(loc[i].first) << "\",\"value\":\"" << Escape(loc[i].second)
           << "\",\"variablesReference\":0}";
    }
    body << "]}";
    return reply(body.str());
  }
  return reply("{}");
}

std::string DapSession::Handle(std::string_view json) {
  auto out = Dispatch(json);
  last_event_ = out;
  return out;
}

void DapSession::Poll() {
  while (!inbox_.empty()) {
    auto msg = std::move(inbox_.front());
    inbox_.pop_front();
    (void)Handle(msg);
  }
}

void DapSession::OnStopped(ScriptDebugger& dbg) {
  last_line_ = dbg.line();
  std::ostringstream o;
  o << "{\"seq\":" << ++seq_ << ",\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1,\"line\":"
    << dbg.line() << "}}";
  last_event_ = o.str();
  Poll();
  if (blocking_) {
    while (dbg.waiting()) {
      Poll();
    }
  }
}

bool DapSession::Listen(std::uint16_t port) {
#if defined(_WIN32)
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    return false;
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 1) != 0) {
    closesocket(s);
    return false;
  }
  listen_sock_ = static_cast<std::intptr_t>(s);
  blocking_ = true;
  return true;
#else
  (void)port;
  return false;
#endif
}

void DapSession::CloseListen() {
#if defined(_WIN32)
  if (listen_sock_ >= 0) {
    closesocket(static_cast<SOCKET>(listen_sock_));
    listen_sock_ = -1;
  }
#endif
}

}  // namespace game_kit
