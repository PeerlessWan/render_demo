#pragma once

// Adapter-only header (not under include/). Samples/business must not include httplib.h.

#include "engine/net/net_system.h"

#include <memory>
#include <string_view>

namespace engine::net {

// Sync remote HTTP(S) via cpp-httplib. Completions are owned by NetSystem Pump queue.
class HttplibHttpClient {
 public:
  HttplibHttpClient() = default;
  Status Get(std::string_view url, HttpResponse& out) const;
  Status Post(std::string_view url, std::string_view body, HttpResponse& out) const;
};

// nullptr when ENGINE_WITH_HTTPLIB=0.
std::unique_ptr<HttplibHttpClient> CreateHttplibHttpClient();

}  // namespace engine::net
