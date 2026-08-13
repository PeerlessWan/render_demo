#include "engine/net/net_system.h"

#include "http_httplib.h"

#include "engine/core/log.h"

#include <mutex>
#include <queue>

namespace engine::net {
namespace {

struct PendingHttp {
  HttpCallback cb;
  Status status = Status::Ok();
  HttpResponse response;
};

// Routes loopback:// locally; http(s):// to HttplibHttpClient (if present).
class RoutingHttp final : public IHttpClient {
 public:
  explicit RoutingHttp(std::unique_ptr<HttplibHttpClient> remote) : remote_(std::move(remote)) {}

  Status Get(std::string_view url, HttpCallback cb) override {
    return Enqueue(url, "", std::move(cb), false);
  }
  Status Post(std::string_view url, std::string_view body, HttpCallback cb) override {
    return Enqueue(url, body, std::move(cb), true);
  }

  void Drain(std::vector<PendingHttp>& out) {
    std::lock_guard lock(mutex_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

 private:
  Status Enqueue(std::string_view url, std::string_view body, HttpCallback cb, bool is_post) {
    PendingHttp p;
    p.cb = std::move(cb);
    const std::string u(url);
    if (u.empty()) {
      p.status = Status::Fail(ErrorCode::InvalidArgument, "empty url");
      p.response.error = p.status.message();
    } else if (u.find("loopback://") == 0) {
      p.status = Status::Ok();
      p.response.status_code = is_post ? 201 : 200;
      p.response.body = is_post ? std::string("echo:") + std::string(body)
                                : std::string("ok:") + u;
    } else if (u.find("http://") == 0 || u.find("https://") == 0) {
      if (!remote_) {
        p.status = Status::Fail(ErrorCode::Unavailable,
                                "HTTP remote not available (ENGINE_WITH_HTTPLIB=0)");
        p.response.error = p.status.message();
      } else if (is_post) {
        p.status = remote_->Post(url, body, p.response);
      } else {
        p.status = remote_->Get(url, p.response);
      }
      if (!p.status) {
        p.response.error = p.status.message();
      }
    } else {
      p.status = Status::Fail(ErrorCode::InvalidArgument,
                              "unsupported URL scheme (use loopback://, http://, or https://)");
      p.response.error = p.status.message();
    }
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(p));
    return Status::Ok();
  }

  std::unique_ptr<HttplibHttpClient> remote_;
  std::mutex mutex_;
  std::vector<PendingHttp> pending_;
};

class LoopbackWebSocket final : public IWebSocket {
 public:
  Status Connect(std::string_view url) override {
    if (std::string(url).find("loopback://") != 0) {
      return Status::Fail(ErrorCode::Unavailable, "WebSocket remote not available");
    }
    connected_ = true;
    return Status::Ok();
  }
  Status Send(std::string_view message) override {
    if (!connected_) {
      return Status::Fail("websocket not connected");
    }
    std::lock_guard lock(mutex_);
    inbox_.emplace(message);
    return Status::Ok();
  }
  Status Close() override {
    connected_ = false;
    return Status::Ok();
  }
  void set_on_message(MessageCallback cb) override { on_message_ = std::move(cb); }

  void Pump() {
    if (!on_message_) {
      return;
    }
    for (;;) {
      std::string msg;
      {
        std::lock_guard lock(mutex_);
        if (inbox_.empty()) {
          break;
        }
        msg = std::move(inbox_.front());
        inbox_.pop();
      }
      on_message_(msg);
    }
  }

 private:
  bool connected_ = false;
  MessageCallback on_message_;
  std::mutex mutex_;
  std::queue<std::string> inbox_;
};

class QuicStub final : public IQuicEndpoint {
 public:
  Status Connect(std::string_view, int) override {
    return Status::Fail(ErrorCode::Unavailable, "QUIC/MsQuic not linked (stub)");
  }
  Status SendReliable(std::string_view) override {
    return Status::Fail(ErrorCode::Unavailable, "QUIC/MsQuic not linked (stub)");
  }
  Status Close() override { return Status::Ok(); }
  bool supported() const override { return false; }
};

}  // namespace

NetSystem::NetSystem()
    : http_(std::make_unique<RoutingHttp>(CreateHttplibHttpClient())),
      ws_(std::make_unique<LoopbackWebSocket>()),
      quic_(std::make_unique<QuicStub>()) {}

NetSystem::~NetSystem() = default;

void NetSystem::Pump() {
  auto* http = static_cast<RoutingHttp*>(http_.get());
  std::vector<PendingHttp> done;
  http->Drain(done);
  for (auto& p : done) {
    if (p.cb) {
      p.cb(p.status, std::move(p.response));
    }
  }
  static_cast<LoopbackWebSocket*>(ws_.get())->Pump();
}

}  // namespace engine::net
