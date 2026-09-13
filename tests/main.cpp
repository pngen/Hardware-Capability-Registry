// Hardware Capability Registry - test harness entry point.
// Copyright 2026 Summon Software Labs.
// SPDX-License-Identifier: Apache-2.0
#include "harness.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace hcrtest {
namespace {

std::string g_current_test;

}  // namespace

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

Registrar::Registrar(const char* suite, const char* name, std::function<void()> body) {
  registry().push_back(TestCase{suite, name, std::move(body)});
}

void fail(const char* file, int line, const std::string& message) {
  std::ostringstream stream;
  stream << file << ":" << line << ": " << message;
  throw Failure(stream.str());
}

int run_all(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.rfind("--suite=", 0) == 0) filter = argument.substr(8);
    if (argument.rfind("--only=", 0) == 0) filter = argument.substr(7);
  }

  std::vector<TestCase> tests = registry();
  std::sort(tests.begin(), tests.end(), [](const TestCase& a, const TestCase& b) {
    if (a.suite != b.suite) return a.suite < b.suite;
    return a.name < b.name;
  });

  std::size_t passed = 0;
  std::size_t failed = 0;
  std::size_t skipped = 0;
  std::string current_suite;
  for (const TestCase& test : tests) {
    const std::string full = test.suite + "." + test.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) continue;
    if (test.suite != current_suite) {
      current_suite = test.suite;
      std::printf("[suite] %s\n", current_suite.c_str());
    }
    g_current_test = full;
    try {
      test.body();
      ++passed;
      std::printf("  ok   %s\n", test.name.c_str());
    } catch (const Skip& skip) {
      ++skipped;
      std::printf("  skip %s (%s)\n", test.name.c_str(), skip.message.c_str());
    } catch (const Failure& failure) {
      ++failed;
      std::printf("  FAIL %s\n       %s\n", test.name.c_str(), failure.message.c_str());
    } catch (const std::exception& error) {
      ++failed;
      std::printf("  FAIL %s\n       unexpected exception: %s\n", test.name.c_str(), error.what());
    } catch (...) {
      ++failed;
      std::printf("  FAIL %s\n       unknown exception\n", test.name.c_str());
    }
  }

  std::printf("\n%d passed, %d failed, %d skipped\n", static_cast<int>(passed), static_cast<int>(failed),
              static_cast<int>(skipped));
  std::fflush(stdout);
  return failed == 0 ? 0 : 1;
}

}  // namespace hcrtest

int main(int argc, char** argv) {
  // Unbuffered output: if a test crashes the process, everything printed up to
  // that point must already be visible, and a lost buffer would hide exactly
  // the diagnostics needed to find the defect.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::fprintf(stderr, "[harness] entering with %d test(s) registered\n", static_cast<int>(hcrtest::registry().size()));
  return hcrtest::run_all(argc, argv);
}
