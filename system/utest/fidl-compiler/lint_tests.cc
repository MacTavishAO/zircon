// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "test_library.h"

namespace {

#define ASSERT_WARNINGS(quantity, lib, content)               \
  do {                                                        \
    const auto& warnings = lib.lints();                       \
    if (strlen(content) != 0) {                               \
      bool contains_content = false;                          \
      for (size_t i = 0; i < warnings.size(); i++) {          \
        if (warnings[i].find(content) != std::string::npos) { \
          contains_content = true;                            \
          break;                                              \
        }                                                     \
      }                                                       \
      ASSERT_TRUE(contains_content, content " not found");    \
    }                                                         \
    if (warnings.size() != quantity) {                        \
      std::string error = "Found warning: ";                  \
      for (size_t i = 0; i < warnings.size(); i++) {          \
        error.append(warnings[i]);                            \
      }                                                       \
      ASSERT_EQ(quantity, warnings.size(), error.c_str());    \
    }                                                         \
  } while (0)

bool const_names_bad() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 bad_CONST = 1234;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_CONST");
  END_TEST;
}

bool const_names_kconst() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fuchsia.a;

const uint64 kAllIsCalm = 1234;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "kAllIsCalm");
  const auto& warnings = library.lints();
  ASSERT_STR_STR(warnings[0].c_str(),
                 "ALL_IS_CALM", "Correct suggestion ALL_IS_CALM not found");
  END_TEST;
}

bool const_names_good() {
  BEGIN_TEST;
  TestLibrary library_yes(R"FIDL(
library fuchsia.a;

const uint64 GOOD_CONST = 1234;

)FIDL");
  ASSERT_TRUE(library_yes.Lint());
  ASSERT_WARNINGS(0, library_yes, "");

  END_TEST;
}

bool protocol_names_bad() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fuchsia.a;

protocol URLLoader {};
)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "URLLoader");
  const auto& warnings = library.lints();
  ASSERT_STR_STR(warnings[0].c_str(),
                 "UrlLoader", "Correct suggestion UrlLoader not found");

  END_TEST;
}

bool protocol_names_good() {
  BEGIN_TEST;

  TestLibrary functioning(R"FIDL(
library fuchsia.a;

protocol UrlLoader {};
)FIDL");
  ASSERT_TRUE(functioning.Lint());
  ASSERT_WARNINGS(0, functioning, "");

  END_TEST;
}

bool library_names_banned_name() {
  BEGIN_TEST;

  TestLibrary banned(R"FIDL(
library fuchsia.zxsocket;
)FIDL");
  ASSERT_FALSE(banned.Lint());
  ASSERT_WARNINGS(1, banned, "zxsocket");

  END_TEST;
}

bool using_names_bad() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fuchsia.a;

using foo as bad_USING;

)FIDL");
  ASSERT_FALSE(library.Lint());
  ASSERT_WARNINGS(1, library, "bad_USING");
  END_TEST;
}

bool using_names_good() {
  BEGIN_TEST;

  TestLibrary library_yes(R"FIDL(
library fuchsia.a;

using foo as good_using;
using bar as baz;

)FIDL");
  ASSERT_TRUE(library_yes.Lint());
  ASSERT_WARNINGS(0, library_yes, "");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(lint_test)
RUN_TEST(const_names_bad)
RUN_TEST(const_names_kconst)
RUN_TEST(const_names_good)
RUN_TEST(protocol_names_bad)
RUN_TEST(protocol_names_good)
RUN_TEST(library_names_banned_name)
RUN_TEST(using_names_bad)
RUN_TEST(using_names_good)
END_TEST_CASE(lint_test)
