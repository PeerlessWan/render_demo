#pragma once

#include <string>
#include <utility>

namespace engine {

enum class ErrorCode {
  Ok = 0,
  Failed,
  InvalidArgument,
  NotFound,
  Unavailable,
};

class Status {
 public:
  static Status Ok() { return Status{ErrorCode::Ok, {}}; }

  // Ok with a short diagnostic (e.g. "simulated-loopback") for stub/demo paths.
  static Status Ok(std::string message) { return Status{ErrorCode::Ok, std::move(message)}; }

  static Status Fail(ErrorCode code, std::string message) {
    return Status{code, std::move(message)};
  }

  static Status Fail(std::string message) { return Fail(ErrorCode::Failed, std::move(message)); }

  [[nodiscard]] bool ok() const { return code_ == ErrorCode::Ok; }
  [[nodiscard]] explicit operator bool() const { return ok(); }
  [[nodiscard]] ErrorCode code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }

 private:
  Status(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

  ErrorCode code_;
  std::string message_;
};

template <typename T>
class Result {
 public:
  static Result Ok(T value) { return Result{std::move(value), Status::Ok()}; }

  static Result Fail(Status status) { return Result{T{}, std::move(status)}; }

  static Result Fail(std::string message) { return Fail(Status::Fail(std::move(message))); }

  [[nodiscard]] bool ok() const { return status_.ok(); }
  [[nodiscard]] explicit operator bool() const { return ok(); }
  [[nodiscard]] const Status& status() const { return status_; }
  [[nodiscard]] T& value() { return value_; }
  [[nodiscard]] const T& value() const { return value_; }
  [[nodiscard]] T* operator->() { return &value_; }
  [[nodiscard]] const T* operator->() const { return &value_; }

 private:
  Result(T value, Status status) : value_(std::move(value)), status_(std::move(status)) {}

  T value_;
  Status status_;
};

}  // namespace engine
