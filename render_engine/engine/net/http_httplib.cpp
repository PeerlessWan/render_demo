#include "http_httplib.h"

#include <stdexcept>
#include <string>

#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB
#include <httplib.h>
#endif

namespace engine::net {
namespace {

#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB

struct ParsedUrl {
  std::string scheme_host_port;
  std::string path_and_query;  // begins with '/'
  bool is_https = false;
};

Status ParseHttpUrl(std::string_view url, ParsedUrl& out) {
  const std::string u(url);
  const auto scheme_end = u.find("://");
  if (scheme_end == std::string::npos) {
    return Status::Fail(ErrorCode::InvalidArgument, "URL missing scheme");
  }
  const std::string scheme = u.substr(0, scheme_end);
  if (scheme != "http" && scheme != "https") {
    return Status::Fail(ErrorCode::InvalidArgument, "unsupported URL scheme (expect http/https)");
  }
  out.is_https = (scheme == "https");

  const std::size_t auth_start = scheme_end + 3;
  if (auth_start >= u.size()) {
    return Status::Fail(ErrorCode::InvalidArgument, "URL missing host");
  }
  const auto path_pos = u.find_first_of("/?#", auth_start);
  if (path_pos == std::string::npos) {
    out.scheme_host_port = u;
    out.path_and_query = "/";
  } else {
    out.scheme_host_port = u.substr(0, path_pos);
    if (u[path_pos] == '/') {
      out.path_and_query = u.substr(path_pos);
    } else {
      out.path_and_query = "/" + u.substr(path_pos);
    }
  }
  return Status::Ok();
}

Status MapHttplibResult(const httplib::Result& res, HttpResponse& out) {
  if (!res) {
    out.status_code = 0;
    out.error = httplib::to_string(res.error());
    return Status::Fail(ErrorCode::Failed, "httplib request failed: " + out.error);
  }
  out.status_code = res->status;
  out.body = res->body;
  out.error.clear();
  return Status::Ok();
}

Status DoRequest(std::string_view url, bool is_post, std::string_view body, HttpResponse& out) {
  ParsedUrl parsed;
  if (auto st = ParseHttpUrl(url, parsed); !st) {
    out.error = st.message();
    return st;
  }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
  if (parsed.is_https) {
    out.error =
        "HTTPS Unavailable: OpenSSL not linked. Configure with ENGINE_WITH_OPENSSL=ON and "
        "OPENSSL_ROOT_DIR pointing at a system OpenSSL SDK (engine does not install OpenSSL).";
    return Status::Fail(ErrorCode::Unavailable, out.error);
  }
#endif

  try {
    httplib::Client cli(parsed.scheme_host_port);
    if (!cli.is_valid()) {
      out.error = "invalid HTTP client for " + parsed.scheme_host_port;
      return Status::Fail(ErrorCode::Unavailable, out.error);
    }
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);
    cli.set_write_timeout(10, 0);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    // Self-signed loopback tests only; public HTTPS still fails on DNS/cert as expected.
    if (parsed.scheme_host_port.find("127.0.0.1") != std::string::npos ||
        parsed.scheme_host_port.find("localhost") != std::string::npos) {
      cli.enable_server_certificate_verification(false);
    }
#endif

    httplib::Result res;
    if (is_post) {
      res = cli.Post(parsed.path_and_query, std::string(body), "application/octet-stream");
    } else {
      res = cli.Get(parsed.path_and_query);
    }
    return MapHttplibResult(res, out);
  } catch (const std::exception& ex) {
    out.error = std::string("httplib exception: ") + ex.what();
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    // HTTPS without OpenSSL throws invalid_argument from Client ctor.
    if (parsed.is_https) {
      out.error =
          "HTTPS Unavailable: OpenSSL not linked. Set OPENSSL_ROOT_DIR + ENGINE_WITH_OPENSSL=ON "
          "(engine does not install OpenSSL). Detail: " +
          std::string(ex.what());
      return Status::Fail(ErrorCode::Unavailable, out.error);
    }
#endif
    return Status::Fail(ErrorCode::Failed, out.error);
  }
}

#endif  // ENGINE_WITH_HTTPLIB

}  // namespace

Status HttplibHttpClient::Get(std::string_view url, HttpResponse& out) const {
#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB
  return DoRequest(url, false, {}, out);
#else
  (void)url;
  out.error = "HTTP remote not available (ENGINE_WITH_HTTPLIB=0)";
  return Status::Fail(ErrorCode::Unavailable, out.error);
#endif
}

Status HttplibHttpClient::Post(std::string_view url, std::string_view body, HttpResponse& out) const {
#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB
  return DoRequest(url, true, body, out);
#else
  (void)url;
  (void)body;
  out.error = "HTTP remote not available (ENGINE_WITH_HTTPLIB=0)";
  return Status::Fail(ErrorCode::Unavailable, out.error);
#endif
}

std::unique_ptr<HttplibHttpClient> CreateHttplibHttpClient() {
#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB
  return std::make_unique<HttplibHttpClient>();
#else
  return nullptr;
#endif
}

}  // namespace engine::net
