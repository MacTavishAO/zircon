// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_info.h>
#include <zxtest/zxtest.h>

namespace test {

struct Struct {
  int a;
  int b;
};

struct StructWithVTable {
  int a;
  int b;

  virtual int Sum() { return a + b; }
};

struct IncompleteType;

}  // namespace test

namespace {

TEST(TypeInfoTest, Basic) {
  EXPECT_STR_EQ(fbl::TypeInfo<int>::Name(), "int");
  EXPECT_STR_EQ(fbl::TypeInfo<double>::Name(), "double");
  EXPECT_STR_EQ(fbl::TypeInfo<test::Struct>::Name(), "test::Struct");
  EXPECT_STR_EQ(fbl::TypeInfo<test::StructWithVTable>::Name(), "test::StructWithVTable");
  EXPECT_STR_EQ(fbl::TypeInfo<test::IncompleteType>::Name(), "test::IncompleteType");

  // Lambdas are printed differently between GCC and Clang. Just test that this
  // expression compiles.
  auto lambda = [](int a, int b) { return a + b; };
  EXPECT_STR_NE(fbl::TypeInfo<decltype(lambda)>::Name(), "");

  char array[10];
  EXPECT_STR_EQ(fbl::TypeInfo<decltype(array)>::Name(), "char [10]");

  char(&array_reference)[10] = array;
  EXPECT_STR_EQ(fbl::TypeInfo<decltype(array_reference)>::Name(), "char (&)[10]");
}

}  // anonymous namespace
