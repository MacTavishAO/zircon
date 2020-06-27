// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <zxtest/zxtest.h>

// Sanity tests that enforce compile time check for printing primitive types, and preventing
// undefined symbols.
TEST(PrintTest, Uint32) {
  uint32_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(PrintTest, Int32) {
  int32_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(PrintTest, Uint64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(PrintTest, Int64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(PrintTest, Float) {
  float a = 0.0;

  ASSERT_EQ(a, 0.0);
}

TEST(PrintTest, Double) {
  double a = 0.0;

  ASSERT_EQ(a, 0.0);
}

TEST(PrintTest, Str) {
  const char* a = "MyStr";

  ASSERT_STR_EQ(a, "MyStr");
}
