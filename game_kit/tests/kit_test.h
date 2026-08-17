#pragma once

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace game_kit::test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& Registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { Registry().push_back(Case{name, fn}); }
};

inline int RunAll() {
  int failed = 0;
  for (const auto& c : Registry()) {
    try {
      c.fn();
      std::cout << "[PASS] " << c.name << '\n';
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "[FAIL] " << c.name << " : " << ex.what() << '\n';
    } catch (...) {
      ++failed;
      std::cerr << "[FAIL] " << c.name << " : unknown exception\n";
    }
  }
  std::cout << (Registry().size() - static_cast<std::size_t>(failed)) << " passed, " << failed
            << " failed\n";
  return failed == 0 ? 0 : 1;
}

struct AssertFailure : std::exception {
  explicit AssertFailure(std::string msg) : message_(std::move(msg)) {}
  const char* what() const noexcept override { return message_.c_str(); }
  std::string message_;
};

}  // namespace game_kit::test

#define KIT_TEST_CONCAT2(a, b) a##b
#define KIT_TEST_CONCAT(a, b) KIT_TEST_CONCAT2(a, b)

#define TEST_CASE(name, tags)                                                            \
  static void KIT_TEST_CONCAT(kit_test_fn_, __LINE__)();                                 \
  static ::game_kit::test::Registrar KIT_TEST_CONCAT(kit_test_reg_, __LINE__)(            \
      name, &KIT_TEST_CONCAT(kit_test_fn_, __LINE__));                                   \
  static void KIT_TEST_CONCAT(kit_test_fn_, __LINE__)()

#define REQUIRE(expr)                                                                     \
  do {                                                                                    \
    if (!(expr)) {                                                                        \
      throw ::game_kit::test::AssertFailure(std::string("REQUIRE failed: ") + #expr);     \
    }                                                                                     \
  } while (0)
