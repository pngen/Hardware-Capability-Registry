// Hardware Capability Registry - test harness.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
//
// Deliberately small: no external dependencies, deterministic ordering, and a
// reproduction seed printed for every randomized failure. No test applies a
// timeout; a hang is a defect, not something the harness hides.
#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "hcr/status.hpp"

namespace hcrtest {

struct Failure : std::exception {
  explicit Failure(std::string text) : message(std::move(text)) {}
  const char* what() const noexcept override { return message.c_str(); }
  std::string message;
};

/// A test that cannot run on this host for an honest, reported reason (for
/// example: no such hardware present). Skips are counted and printed; they are
/// never silently treated as passes.
struct Skip {
  explicit Skip(std::string text) : message(std::move(text)) {}
  std::string message;
};

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> body);
};

void fail(const char* file, int line, const std::string& message);

int run_all(int argc, char** argv);

/// Deterministic xorshift64* generator. Every randomized test prints its seed.
class Random {
 public:
  explicit Random(std::uint64_t seed) : state_(seed == 0 ? 0x9E3779B97F4A7C15ull : seed) {}

  std::uint64_t next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545F4914F6CDD1Dull;
  }
  std::uint64_t bounded(std::uint64_t bound) { return bound == 0 ? 0 : next() % bound; }
  int between(int low, int high) {
    if (high <= low) return low;
    return low + static_cast<int>(bounded(static_cast<std::uint64_t>(high - low + 1)));
  }
  bool chance(unsigned numerator, unsigned denominator) { return bounded(denominator) < numerator; }
  [[nodiscard]] std::uint64_t seed() const noexcept { return state_; }

 private:
  std::uint64_t state_;
};

}  // namespace hcrtest

#define HCR_TEST(suite, name)                                                                       \
  void suite##_##name##_body();                                                                     \
  static ::hcrtest::Registrar suite##_##name##_registrar(#suite, #name, suite##_##name##_body);     \
  void suite##_##name##_body()

#define HCR_CHECK(condition)                                                                        \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      ::hcrtest::fail(__FILE__, __LINE__, "CHECK failed: " #condition);                             \
    }                                                                                               \
  } while (false)

#define HCR_REQUIRE(condition)                                                                      \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      ::hcrtest::fail(__FILE__, __LINE__, "REQUIRE failed: " #condition);                           \
      throw ::hcrtest::Failure("REQUIRE failed: " #condition);                                      \
    }                                                                                               \
  } while (false)

#define HCR_REQUIRE_MSG(condition, message)                                                         \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::ostringstream hcr_stream;                                                                \
      hcr_stream << "REQUIRE failed: " #condition << " :: " << message;                             \
      ::hcrtest::fail(__FILE__, __LINE__, hcr_stream.str());                                        \
      throw ::hcrtest::Failure(hcr_stream.str());                                                   \
    }                                                                                               \
  } while (false)

#define HCR_CHECK_MSG(condition, message)                                                           \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::ostringstream hcr_stream;                                                                \
      hcr_stream << "CHECK failed: " #condition << " :: " << message;                               \
      ::hcrtest::fail(__FILE__, __LINE__, hcr_stream.str());                                        \
    }                                                                                               \
  } while (false)

#define HCR_CHECK_EQ(actual, expected)                                                              \
  do {                                                                                              \
    const auto& hcr_actual = (actual);                                                              \
    const auto& hcr_expected = (expected);                                                          \
    if (!(hcr_actual == hcr_expected)) {                                                            \
      std::ostringstream hcr_stream;                                                                \
      hcr_stream << "CHECK_EQ failed: " #actual " == " #expected;                                   \
      ::hcrtest::fail(__FILE__, __LINE__, hcr_stream.str());                                        \
    }                                                                                               \
  } while (false)

#define HCR_CHECK_STATUS(expression)                                                                \
  do {                                                                                              \
    const ::hcr::Status hcr_status = (expression);                                                  \
    if (!hcr_status.ok()) {                                                                         \
      ::hcrtest::fail(__FILE__, __LINE__, std::string("unexpected status: ") + hcr_status.describe()); \
    }                                                                                               \
  } while (false)

#define HCR_CHECK_STATUS_EQ(expression, expected_code)                                              \
  do {                                                                                              \
    const ::hcr::Status hcr_status = (expression);                                                  \
    if (hcr_status.code() != (expected_code)) {                                                     \
      ::hcrtest::fail(__FILE__, __LINE__,                                                           \
                      std::string("expected ") + ::hcr::to_string(expected_code) + " but got " +    \
                          hcr_status.describe());                                                   \
    }                                                                                               \
  } while (false)

#define HCR_SKIP(reason) throw ::hcrtest::Skip(reason)
