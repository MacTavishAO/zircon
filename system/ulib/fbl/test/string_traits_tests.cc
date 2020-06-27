// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/string_traits.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFakeStringData[] = "hello";
constexpr size_t kFakeStringLength = std::size(kFakeStringData);

struct SimpleFakeString {
  const char* data() const { return kFakeStringData; }
  size_t length() const { return kFakeStringLength; }
};

struct OverloadedFakeString {
  const char* data() const { return kFakeStringData; }
  size_t length() const { return kFakeStringLength; }

  // These are decoys to verify that the conversion operator only considers
  // the const overloads of these members.
  void data();
  void length();
};

struct EmptyStructBadString {};

struct DataOnlyBadString {
  const char* data();
};

struct LengthOnlyBadString {
  size_t length();
};

struct WrongDataTypeBadString {
  char* data() const;
  size_t length() const;
};

struct WrongLengthTypeBadString {
  const char* data() const;
  int32_t length() const;
};

static_assert(fbl::is_string_like_v<fbl::String>, "ok - string");
static_assert(fbl::is_string_like_v<fbl::StringPiece>, "ok - string piece");
static_assert(fbl::is_string_like_v<SimpleFakeString>, "ok - simple");
static_assert(fbl::is_string_like_v<OverloadedFakeString>, "ok - overloaded");
static_assert(!fbl::is_string_like_v<decltype(nullptr)>, "bad - null");
static_assert(!fbl::is_string_like_v<int>, "bad - int");
static_assert(!fbl::is_string_like_v<EmptyStructBadString>, "bad - empty struct");
static_assert(!fbl::is_string_like_v<DataOnlyBadString>, "bad - data only");
static_assert(!fbl::is_string_like_v<LengthOnlyBadString>, "bad - length only");
static_assert(!fbl::is_string_like_v<WrongDataTypeBadString>, "bad - wrong data type");
static_assert(!fbl::is_string_like_v<WrongLengthTypeBadString>, "bad - wrong length type");

TEST(StringTraitsTest, Accessor) {
  {
    SimpleFakeString str;
    EXPECT_EQ(kFakeStringData, fbl::GetStringData(str));
    EXPECT_EQ(kFakeStringLength, fbl::GetStringLength(str));
  }

  {
    OverloadedFakeString str;
    EXPECT_EQ(kFakeStringData, fbl::GetStringData(str));
    EXPECT_EQ(kFakeStringLength, fbl::GetStringLength(str));
  }
}

}  // namespace
