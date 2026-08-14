#include "engine/core/log.h"
#include "engine/debug/sandbox_harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void Write(const std::string& body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

std::string ReadMessage() {
  std::string header;
  int content_length = -1;
  while (std::getline(std::cin, header)) {
    if (!header.empty() && header.back() == '\r') {
      header.pop_back();
    }
    if (header.empty()) {
      break;
    }
    if (header.rfind("Content-Length:", 0) == 0) {
      content_length = std::atoi(header.c_str() + 15);
    }
  }
  if (content_length <= 0) {
    return {};
  }
  std::string body(static_cast<std::size_t>(content_length), '\0');
  std::cin.read(body.data(), content_length);
  return body;
}

std::string EscapeJson(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

std::string FindSandboxExe() {
  if (const char* env = std::getenv("ENGINE_SANDBOX_EXE"); env && env[0] != '\0') {
    return env;
  }
  // Relative to typical build layout next to sandbox_mcp.
  const char* candidates[] = {
      "sample_sandbox.exe",
      "../samples/Sandbox/Debug/sample_sandbox.exe",
      "../../samples/Sandbox/Debug/sample_sandbox.exe",
      "../Sandbox/Debug/sample_sandbox.exe",
  };
  for (const char* c : candidates) {
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesA(c);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      return c;
    }
#else
    if (access(c, X_OK) == 0) {
      return c;
    }
#endif
  }
  return {};
}

class SandboxPipe {
 public:
  bool Start(const std::string& exe) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out_r = nullptr, out_w = nullptr;
    HANDLE in_r = nullptr, in_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&in_r, &in_w, &sa, 0)) {
      return false;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::string cmd = "\"" + exe + "\" --harness-stdio --backend=d3d12";
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
      CloseHandle(out_r);
      CloseHandle(out_w);
      CloseHandle(in_r);
      CloseHandle(in_w);
      return false;
    }
    CloseHandle(out_w);
    CloseHandle(in_r);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    out_ = out_r;
    in_ = in_w;
    return true;
#else
    (void)exe;
    return false;
#endif
  }

  std::string Call(const std::string& line) {
#if defined(_WIN32)
    if (!in_ || !out_) {
      return engine::debug::HarnessErr("sandbox not started");
    }
    const std::string payload = line + "\n";
    DWORD written = 0;
    if (!WriteFile(in_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) {
      return engine::debug::HarnessErr("write harness failed");
    }
    std::string resp;
    char buf[4096];
    for (;;) {
      DWORD n = 0;
      if (!ReadFile(out_, buf, sizeof(buf) - 1, &n, nullptr) || n == 0) {
        break;
      }
      buf[n] = '\0';
      resp.append(buf, buf + n);
      const auto nl = resp.find('\n');
      if (nl != std::string::npos) {
        std::string line = resp.substr(0, nl);
        while (!line.empty() && (line.back() == '\r')) {
          line.pop_back();
        }
        if (!line.empty() && line.front() == '{') {
          return line;
        }
        resp.erase(0, nl + 1);
      }
    }
    return resp.empty() ? engine::debug::HarnessErr("empty harness response") : resp;
#else
    (void)line;
    return engine::debug::HarnessErr("sandbox pipe unsupported");
#endif
  }

  void Stop() {
#if defined(_WIN32)
    if (in_) {
      const char* quit = "{\"cmd\":\"quit\"}\n";
      DWORD written = 0;
      WriteFile(in_, quit, static_cast<DWORD>(std::strlen(quit)), &written, nullptr);
      CloseHandle(in_);
      in_ = nullptr;
    }
    if (out_) {
      CloseHandle(out_);
      out_ = nullptr;
    }
    if (process_) {
      WaitForSingleObject(process_, 3000);
      CloseHandle(process_);
      process_ = nullptr;
    }
#endif
  }

  ~SandboxPipe() { Stop(); }

  [[nodiscard]] bool ready() const {
#if defined(_WIN32)
    return process_ != nullptr;
#else
    return false;
#endif
  }

 private:
#if defined(_WIN32)
  HANDLE process_ = nullptr;
  HANDLE in_ = nullptr;
  HANDLE out_ = nullptr;
#endif
};

std::string ToolName(const std::string& msg) {
  const auto pos = msg.find("\"name\"");
  if (pos == std::string::npos) {
    return {};
  }
  const auto q1 = msg.find('"', pos + 6);
  if (q1 == std::string::npos) {
    return {};
  }
  const auto q2 = msg.find('"', q1 + 1);
  if (q2 == std::string::npos) {
    return {};
  }
  return msg.substr(q1 + 1, q2 - q1 - 1);
}

std::string ArgValue(const std::string& msg, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  const auto pos = msg.find(needle);
  if (pos == std::string::npos) {
    return {};
  }
  const auto colon = msg.find(':', pos);
  const auto q1 = msg.find('"', colon);
  if (q1 == std::string::npos) {
    return {};
  }
  const auto q2 = msg.find('"', q1 + 1);
  if (q2 == std::string::npos) {
    return {};
  }
  return msg.substr(q1 + 1, q2 - q1 - 1);
}

}  // namespace

int main() {
  engine::LogInfo("sandbox_mcp ready (stdio MCP <-> live Sandbox harness)");
  SandboxPipe sandbox;
  const std::string exe = FindSandboxExe();
  if (!exe.empty()) {
    if (!sandbox.Start(exe)) {
      engine::LogWarn("sandbox_mcp: failed to spawn " + exe + " — tools return offline hints");
    } else {
      engine::LogInfo("sandbox_mcp: spawned " + exe);
    }
  } else {
    engine::LogWarn("sandbox_mcp: set ENGINE_SANDBOX_EXE to sample_sandbox path");
  }

  for (;;) {
    const std::string msg = ReadMessage();
    if (msg.empty()) {
      break;
    }
    int id = 1;
    const auto id_pos = msg.find("\"id\"");
    if (id_pos != std::string::npos) {
      id = std::atoi(msg.c_str() + id_pos + 5);
    }

    if (msg.find("initialize") != std::string::npos &&
        msg.find("notifications/initialized") == std::string::npos) {
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
            "\"serverInfo\":{\"name\":\"sandbox_mcp\",\"version\":\"0.2.0\"}}}");
      continue;
    }
    if (msg.find("initialized") != std::string::npos) {
      continue;
    }
    if (msg.find("tools/list") != std::string::npos) {
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"tools\":["
            "{\"name\":\"ping\",\"description\":\"Harness ping\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"query_features\",\"description\":\"Query features\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"toggle\",\"description\":\"Toggle fx key\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"}}}},"
            "{\"name\":\"set_quality\",\"description\":\"Set quality tier\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"}}}},"
            "{\"name\":\"frame\",\"description\":\"Ack frame\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"profiler_snapshot\",\"description\":\"CPU profiler samples\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"capture\",\"description\":\"Capture RGBA path\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}"
            "]}}");
      continue;
    }
    if (msg.find("tools/call") != std::string::npos) {
      std::string text;
      const std::string tool = ToolName(msg);
      if (!sandbox.ready()) {
        text = engine::debug::HarnessErr("sandbox offline — set ENGINE_SANDBOX_EXE");
      } else if (tool == "ping") {
        text = sandbox.Call(R"({"cmd":"ping"})");
      } else if (tool == "query_features") {
        text = sandbox.Call(R"({"cmd":"query_features"})");
      } else if (tool == "toggle") {
        const std::string key = ArgValue(msg, "key");
        text = sandbox.Call(R"({"cmd":"toggle","key":")" + (key.empty() ? "taa" : key) + "\"}");
      } else if (tool == "set_quality") {
        const std::string key = ArgValue(msg, "key");
        text = sandbox.Call(R"({"cmd":"set_quality","key":")" + (key.empty() ? "medium" : key) +
                            "\"}");
      } else if (tool == "frame") {
        text = sandbox.Call(R"({"cmd":"frame","n":1})");
      } else if (tool == "profiler_snapshot") {
        text = sandbox.Call(R"({"cmd":"profiler_snapshot"})");
      } else if (tool == "capture") {
        const std::string path = ArgValue(msg, "path");
        const std::string out =
            path.empty() ? std::string("build/mcp_capture.rgba") : path;
        text = sandbox.Call(R"({"cmd":"capture","path":")" + out + "\"}");
      } else {
        text = engine::debug::HarnessErr("unknown tool");
      }
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" + EscapeJson(text) +
            "\"}]}}");
      continue;
    }
    Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
          ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
  }
  sandbox.Stop();
  return 0;
}
