#pragma once

#include "engine/core/result.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::net {

struct HttpResponse {
  int status_code = 0;
  std::string body;
  std::string error;
};

using HttpCallback = std::function<void(Status, HttpResponse)>;

class IHttpClient {
 public:
  virtual ~IHttpClient() = default;
  virtual Status Get(std::string_view url, HttpCallback cb) = 0;
  virtual Status Post(std::string_view url, std::string_view body, HttpCallback cb) = 0;
};

class IWebSocket {
 public:
  virtual ~IWebSocket() = default;
  virtual Status Connect(std::string_view url) = 0;
  virtual Status Send(std::string_view message) = 0;
  virtual Status Close() = 0;
  using MessageCallback = std::function<void(std::string_view)>;
  virtual void set_on_message(MessageCallback cb) = 0;
};

class IQuicEndpoint {
 public:
  virtual ~IQuicEndpoint() = default;
  virtual Status Connect(std::string_view host, int port) = 0;
  virtual Status SendReliable(std::string_view data) = 0;
  virtual Status Close() = 0;
  [[nodiscard]] virtual bool supported() const { return false; }
};

// Loopback-capable net hub. Completions only after Pump() (HOSTING).
class NetSystem {
 public:
  NetSystem();
  ~NetSystem();

  IHttpClient& http() { return *http_; }
  IWebSocket& websocket() { return *ws_; }
  IQuicEndpoint& quic() { return *quic_; }

  void Pump();

 private:
  std::unique_ptr<IHttpClient> http_;
  std::unique_ptr<IWebSocket> ws_;
  std::unique_ptr<IQuicEndpoint> quic_;
};

}  // namespace engine::net
