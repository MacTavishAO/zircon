// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reader_tests.h"

#include <stdint.h>

#include <iterator>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <trace-reader/reader.h>
#include <zxtest/zxtest.h>

namespace trace {
namespace {

TEST(TraceReader, EmptyChunk) {
  uint64_t value;
  int64_t int64_value;
  double double_value;
  fbl::StringPiece string_value;
  trace::Chunk subchunk;

  trace::Chunk empty;
  EXPECT_EQ(0u, empty.remaining_words());

  EXPECT_FALSE(empty.ReadUint64(&value));

  EXPECT_FALSE(empty.ReadInt64(&int64_value));

  EXPECT_FALSE(empty.ReadDouble(&double_value));

  EXPECT_TRUE(empty.ReadString(0u, &string_value));
  EXPECT_TRUE(string_value.empty());
  EXPECT_FALSE(empty.ReadString(1u, &string_value));

  EXPECT_TRUE(empty.ReadChunk(0u, &subchunk));
  EXPECT_EQ(0u, subchunk.remaining_words());
  EXPECT_FALSE(empty.ReadChunk(1u, &subchunk));
}

TEST(TraceReader, NonEmptyChunk) {
  uint64_t value;
  int64_t int64_value;
  double double_value;
  fbl::StringPiece string_value;
  trace::Chunk subchunk;

  uint64_t kData[] = {
      // uint64 values
      0,
      UINT64_MAX,
      // int64 values
      test::ToWord(INT64_MIN),
      test::ToWord(INT64_MAX),
      // double values
      test::ToWord(1.5),
      test::ToWord(-3.14),
      // string values (will be filled in)
      0,
      0,
      // sub-chunk values
      123,
      456,
      // more stuff beyond sub-chunk
      789,
  };
  memcpy(kData + 6, "Hello World!----", 16);

  trace::Chunk chunk(kData, std::size(kData));
  EXPECT_EQ(std::size(kData), chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadUint64(&value));
  EXPECT_EQ(0, value);
  EXPECT_EQ(10u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadUint64(&value));
  EXPECT_EQ(UINT64_MAX, value);
  EXPECT_EQ(9u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadInt64(&int64_value));
  EXPECT_EQ(INT64_MIN, int64_value);
  EXPECT_EQ(8u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadInt64(&int64_value));
  EXPECT_EQ(INT64_MAX, int64_value);
  EXPECT_EQ(7u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadDouble(&double_value));
  EXPECT_EQ(1.5, double_value);
  EXPECT_EQ(6u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadDouble(&double_value));
  EXPECT_EQ(-3.14, double_value);
  EXPECT_EQ(5u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadString(0u, &string_value));
  EXPECT_TRUE(string_value.empty());
  EXPECT_EQ(5u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadString(12u, &string_value));
  EXPECT_EQ(12u, string_value.length());
  EXPECT_EQ(reinterpret_cast<const char*>(kData + 6), string_value.data());
  EXPECT_TRUE(fbl::String(string_value) == "Hello World!");
  EXPECT_EQ(3u, chunk.remaining_words());

  EXPECT_TRUE(chunk.ReadChunk(2u, &subchunk));
  EXPECT_EQ(2u, subchunk.remaining_words());
  EXPECT_TRUE(subchunk.ReadUint64(&value));

  EXPECT_EQ(123, value);
  EXPECT_EQ(1u, subchunk.remaining_words());

  EXPECT_TRUE(chunk.ReadUint64(&value));
  EXPECT_EQ(789, value);
  EXPECT_EQ(0u, chunk.remaining_words());

  EXPECT_TRUE(subchunk.ReadUint64(&value));
  EXPECT_EQ(456, value);
  EXPECT_EQ(0u, subchunk.remaining_words());

  EXPECT_FALSE(subchunk.ReadUint64(&value));
  EXPECT_FALSE(chunk.ReadUint64(&value));
}

TEST(TraceReader, InitialState) {
  fbl::Vector<trace::Record> records;
  fbl::String error;
  trace::TraceReader reader(test::MakeRecordConsumer(&records), test::MakeErrorHandler(&error));

  EXPECT_EQ(0, reader.current_provider_id());
  EXPECT_TRUE(reader.current_provider_name() == "");
  EXPECT_TRUE(reader.GetProviderName(0) == "");
  EXPECT_EQ(0, records.size());
  EXPECT_TRUE(error.empty());
}

TEST(TraceReader, EmptyBuffer) {
  fbl::Vector<trace::Record> records;
  fbl::String error;
  trace::TraceReader reader(test::MakeRecordConsumer(&records), test::MakeErrorHandler(&error));

  trace::Chunk empty;
  EXPECT_TRUE(reader.ReadRecords(empty));
  EXPECT_EQ(0, records.size());
  EXPECT_TRUE(error.empty());
}

// NOTE: Most of the reader is covered by the libtrace tests.

}  // namespace
}  // namespace trace
