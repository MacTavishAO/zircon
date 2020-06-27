// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <zxtest/zxtest.h>

namespace bitmap {
namespace tests {

template <typename RawBitmap>
static void InitializedEmpty(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(0), ZX_OK);
  EXPECT_EQ(bitmap.size(), 0U, "get size");

  EXPECT_TRUE(bitmap.GetOne(0), "get one bit");
  EXPECT_EQ(bitmap.SetOne(0), ZX_ERR_INVALID_ARGS, "set one bit");
  EXPECT_EQ(bitmap.ClearOne(0), ZX_ERR_INVALID_ARGS, "clear one bit");

  EXPECT_EQ(bitmap.Reset(1), ZX_OK);
  EXPECT_FALSE(bitmap.GetOne(0), "get one bit");
  EXPECT_EQ(bitmap.SetOne(0), ZX_OK, "set one bit");
  EXPECT_EQ(bitmap.ClearOne(0), ZX_OK, "clear one bit");
}

template <typename RawBitmap>
static void SingleBit(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_FALSE(bitmap.GetOne(2), "get bit before setting");

  EXPECT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
  EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

  EXPECT_EQ(bitmap.ClearOne(2), ZX_OK, "clear bit");
  EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");
}

template <typename RawBitmap>
static void SetTwice(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
  EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting");

  EXPECT_EQ(bitmap.SetOne(2), ZX_OK, "set bit again");
  EXPECT_TRUE(bitmap.GetOne(2), "get bit after setting again");
}

template <typename RawBitmap>
static void ClearTwice(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");

  EXPECT_EQ(bitmap.ClearOne(2), ZX_OK, "clear bit");
  EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing");

  EXPECT_EQ(bitmap.ClearOne(2), ZX_OK, "clear bit again");
  EXPECT_FALSE(bitmap.GetOne(2), "get bit after clearing again");
}

template <typename RawBitmap>
static void GetReturnArg(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  size_t first_unset = 0;
  EXPECT_FALSE(bitmap.Get(2, 3, nullptr), "get bit with null");
  EXPECT_FALSE(bitmap.Get(2, 3, &first_unset), "get bit with nonnull");
  EXPECT_EQ(first_unset, 2U, "check returned arg");

  EXPECT_EQ(bitmap.SetOne(2), ZX_OK, "set bit");
  EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get bit after setting");
  EXPECT_EQ(first_unset, 3U, "check returned arg");

  first_unset = 0;
  EXPECT_FALSE(bitmap.Get(2, 4, &first_unset), "get larger range after setting");
  EXPECT_EQ(first_unset, 3U, "check returned arg");

  EXPECT_EQ(bitmap.SetOne(3), ZX_OK, "set another bit");
  EXPECT_FALSE(bitmap.Get(2, 5, &first_unset), "get larger range after setting another");
  EXPECT_EQ(first_unset, 4U, "check returned arg");
}

template <typename RawBitmap>
static void SetRange(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");

  size_t first_unset = 0;
  EXPECT_TRUE(bitmap.Get(2, 3, &first_unset), "get first bit in range");
  EXPECT_EQ(first_unset, 3U, "check returned arg");

  EXPECT_TRUE(bitmap.Get(99, 100, &first_unset), "get last bit in range");
  EXPECT_EQ(first_unset, 100U, "check returned arg");

  EXPECT_FALSE(bitmap.Get(1, 2, &first_unset), "get bit before first in range");
  EXPECT_EQ(first_unset, 1U, "check returned arg");

  EXPECT_FALSE(bitmap.Get(100, 101, &first_unset), "get bit after last in range");
  EXPECT_EQ(first_unset, 100U, "check returned arg");

  EXPECT_TRUE(bitmap.Get(2, 100, &first_unset), "get entire range");
  EXPECT_EQ(first_unset, 100U, "check returned arg");

  EXPECT_TRUE(bitmap.Get(50, 80, &first_unset), "get part of range");
  EXPECT_EQ(first_unset, 80U, "check returned arg");

  size_t result;
  EXPECT_FALSE(bitmap.Scan(0, 100, true, &result), "scan set bits");
  EXPECT_EQ(result, 0U, "scan set bits");
  EXPECT_FALSE(bitmap.ReverseScan(0, 100, true, &result), "reverse scan set bits");
  EXPECT_EQ(result, 1U, "reverse scan set bits");

  EXPECT_FALSE(bitmap.Scan(0, 100, false, &result), "scan cleared bits");
  EXPECT_EQ(result, 2U, "scan cleared bits to start");
  EXPECT_FALSE(bitmap.ReverseScan(0, 100, false, &result), "reverse scan cleared bits");
  EXPECT_EQ(result, 99U, "reverse scan cleared bits");

  EXPECT_TRUE(bitmap.Scan(2, 100, true), "scan set bits in set range");
  EXPECT_TRUE(bitmap.ReverseScan(2, 100, true), "reverse scan set bits in set range");

  EXPECT_FALSE(bitmap.Scan(2, 100, false, &result), "scan cleared bits in set range");
  EXPECT_EQ(result, 2U, "scan cleared bits in set range");
  EXPECT_FALSE(bitmap.ReverseScan(2, 100, false, &result),
               "reverse scan cleared bits in set range");
  EXPECT_EQ(result, 99U, "reverse scan cleared bits in set range");

  EXPECT_TRUE(bitmap.Scan(50, 80, true), "scan set bits in subrange");
  EXPECT_TRUE(bitmap.ReverseScan(50, 80, true), "reverse scan set bits in subrange");

  EXPECT_TRUE(bitmap.Scan(100, 200, false), "scan past end of bitmap");
  EXPECT_TRUE(bitmap.ReverseScan(100, 200, false), "reverse scan past end of bitmap");
}

template <typename RawBitmap>
static void FindSimple(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  size_t bitoff_start;

  // Invalid finds
  EXPECT_EQ(bitmap.Find(false, 0, 0, 1, &bitoff_start), ZX_ERR_INVALID_ARGS, "bad range");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 0, 1, &bitoff_start), ZX_ERR_INVALID_ARGS, "bad range");
  EXPECT_EQ(bitmap.Find(false, 1, 0, 1, &bitoff_start), ZX_ERR_INVALID_ARGS, "bad range");
  EXPECT_EQ(bitmap.ReverseFind(false, 1, 0, 1, &bitoff_start), ZX_ERR_INVALID_ARGS, "bad range");
  EXPECT_EQ(bitmap.Find(false, 0, 1, 1, nullptr), ZX_ERR_INVALID_ARGS, "bad output");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 1, 1, nullptr), ZX_ERR_INVALID_ARGS, "bad output");

  // Finds from offset zero
  EXPECT_EQ(bitmap.Find(false, 0, 100, 1, &bitoff_start), ZX_OK, "find unset");
  EXPECT_EQ(bitoff_start, 0, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 100, 1, &bitoff_start), ZX_OK, "reverse find unset");
  EXPECT_EQ(bitoff_start, 99, "check returned arg");

  EXPECT_EQ(bitmap.Find(true, 0, 100, 1, &bitoff_start), ZX_ERR_NO_RESOURCES, "find set");
  EXPECT_EQ(bitmap.ReverseFind(true, 0, 100, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse find set");

  EXPECT_EQ(bitmap.Find(false, 0, 100, 5, &bitoff_start), ZX_OK, "find more unset");
  EXPECT_EQ(bitoff_start, 0, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 100, 5, &bitoff_start), ZX_OK, "reverse find more unset");
  EXPECT_EQ(bitoff_start, 95, "check returned arg");

  EXPECT_EQ(bitmap.Find(true, 0, 100, 5, &bitoff_start), ZX_ERR_NO_RESOURCES, "find more set");
  EXPECT_EQ(bitmap.ReverseFind(true, 0, 100, 5, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse find more set");

  EXPECT_EQ(bitmap.Find(false, 0, 100, 100, &bitoff_start), ZX_OK, "find all unset");
  EXPECT_EQ(bitoff_start, 0, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 100, 100, &bitoff_start), ZX_OK, "reverse find all unset");
  EXPECT_EQ(bitoff_start, 0, "check returned arg");

  EXPECT_EQ(bitmap.Find(true, 0, 100, 100, &bitoff_start), ZX_ERR_NO_RESOURCES, "find all set");
  EXPECT_EQ(bitmap.ReverseFind(true, 0, 100, 100, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse find all set");

  // Finds at an offset
  EXPECT_EQ(bitmap.Find(false, 50, 100, 3, &bitoff_start), ZX_OK, "find at offset");
  EXPECT_EQ(bitoff_start, 50, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 50, 100, 3, &bitoff_start), ZX_OK, "reverse find at offset");
  EXPECT_EQ(bitoff_start, 97, "check returned arg");

  EXPECT_EQ(bitmap.Find(true, 50, 100, 3, &bitoff_start), ZX_ERR_NO_RESOURCES, "fail at offset");
  EXPECT_EQ(bitmap.ReverseFind(true, 50, 100, 3, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail at offset");

  EXPECT_EQ(bitmap.Find(false, 90, 100, 10, &bitoff_start), ZX_OK, "find at offset end");
  EXPECT_EQ(bitoff_start, 90, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 90, 100, 10, &bitoff_start), ZX_OK,
            "reverse find at offset end");
  EXPECT_EQ(bitoff_start, 90, "check returned arg");

  // Invalid scans
  EXPECT_EQ(bitmap.Find(false, 0, 100, 101, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 100, 101, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.Find(false, 91, 100, 10, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.ReverseFind(false, 91, 100, 10, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.Find(false, 90, 100, 11, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.ReverseFind(false, 90, 100, 11, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.Find(false, 90, 95, 6, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");
  EXPECT_EQ(bitmap.ReverseFind(false, 90, 95, 6, &bitoff_start), ZX_ERR_NO_RESOURCES, "no space");

  // Fill the bitmap
  EXPECT_EQ(bitmap.Set(5, 10), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Set(20, 30), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Set(32, 35), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Set(90, 95), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Set(70, 80), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Set(65, 68), ZX_OK, "set range");

  EXPECT_EQ(bitmap.Find(false, 0, 50, 5, &bitoff_start), ZX_OK, "find in first group");
  EXPECT_EQ(bitoff_start, 0, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 50, 100, 5, &bitoff_start), ZX_OK,
            "reverse find in first group");
  EXPECT_EQ(bitoff_start, 95, "check returned arg");

  EXPECT_EQ(bitmap.Find(false, 0, 50, 10, &bitoff_start), ZX_OK, "find in second group");
  EXPECT_EQ(bitoff_start, 10, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 50, 100, 10, &bitoff_start), ZX_OK,
            "reverse find in second group");
  EXPECT_EQ(bitoff_start, 80, "check returned arg");

  EXPECT_EQ(bitmap.Find(false, 0, 50, 15, &bitoff_start), ZX_OK, "find in third group");
  EXPECT_EQ(bitoff_start, 35, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 50, 100, 15, &bitoff_start), ZX_OK,
            "reverse find in third group");
  EXPECT_EQ(bitoff_start, 50, "check returned arg");

  EXPECT_EQ(bitmap.Find(false, 0, 50, 16, &bitoff_start), ZX_ERR_NO_RESOURCES, "fail to find");
  EXPECT_EQ(bitmap.ReverseFind(false, 50, 100, 16, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find");

  EXPECT_EQ(bitmap.Find(false, 5, 20, 10, &bitoff_start), ZX_OK, "find space (offset)");
  EXPECT_EQ(bitoff_start, 10, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 80, 95, 10, &bitoff_start), ZX_OK,
            "reverse find space (offset)");
  EXPECT_EQ(bitoff_start, 80, "check returned arg");

  EXPECT_EQ(bitmap.Find(false, 5, 25, 10, &bitoff_start), ZX_OK, "find space (offset)");
  EXPECT_EQ(bitoff_start, 10, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(false, 75, 95, 10, &bitoff_start), ZX_OK,
            "reverse find space (offset)");
  EXPECT_EQ(bitoff_start, 80, "check returned arg");

  EXPECT_EQ(bitmap.Find(false, 5, 15, 6, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "fail to find (offset)");
  EXPECT_EQ(bitmap.ReverseFind(false, 85, 95, 6, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find (offset)");

  EXPECT_EQ(bitmap.Find(true, 0, 15, 2, &bitoff_start), ZX_OK, "find set bits");
  EXPECT_EQ(bitoff_start, 5, "check returned arg");
  EXPECT_EQ(bitmap.ReverseFind(true, 85, 100, 2, &bitoff_start), ZX_OK, "reverse find set bits");
  EXPECT_EQ(bitoff_start, 93, "check returned arg");

  EXPECT_EQ(bitmap.Find(true, 0, 15, 6, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "find set bits (fail)");
  EXPECT_EQ(bitmap.ReverseFind(true, 85, 100, 6, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse find set bits (fail)");

  EXPECT_EQ(bitmap.Find(false, 32, 35, 3, &bitoff_start), ZX_ERR_NO_RESOURCES, "fail to find");
  EXPECT_EQ(bitmap.ReverseFind(false, 65, 68, 3, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find");

  EXPECT_EQ(bitmap.Find(false, 32, 35, 4, &bitoff_start), ZX_ERR_NO_RESOURCES, "fail to find");
  EXPECT_EQ(bitmap.ReverseFind(false, 65, 68, 4, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find");

  EXPECT_EQ(bitmap.Find(true, 32, 35, 4, &bitoff_start), ZX_ERR_NO_RESOURCES, "fail to find (set)");
  EXPECT_EQ(bitmap.ReverseFind(true, 65, 68, 4, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find (set)");

  // Fill the whole bitmap
  EXPECT_EQ(bitmap.Set(0, 128), ZX_OK, "set range");

  EXPECT_EQ(bitmap.Find(false, 0, 1, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "fail to find (small)");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 1, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find (small)");

  EXPECT_EQ(bitmap.Find(false, 0, 128, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "fail to find (large)");
  EXPECT_EQ(bitmap.ReverseFind(false, 0, 128, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "reverse fail to find (large)");
}

template <typename RawBitmap>
static void ClearAll(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.Set(0, 100), ZX_OK, "set range");

  bitmap.ClearAll();

  size_t first = 0;
  EXPECT_FALSE(bitmap.Get(2, 100, &first), "get range");
  EXPECT_EQ(first, 2U, "all clear");

  EXPECT_EQ(bitmap.Set(0, 99), ZX_OK, "set range");
  EXPECT_FALSE(bitmap.Get(0, 100, &first), "get range");
  EXPECT_EQ(first, 99U, "all clear");
}

template <typename RawBitmap>
static void ClearSubrange(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.Set(2, 100), ZX_OK, "set range");
  EXPECT_EQ(bitmap.Clear(50, 80), ZX_OK, "clear range");

  size_t first_unset = 0;
  EXPECT_FALSE(bitmap.Get(2, 100, &first_unset), "get whole original range");
  EXPECT_EQ(first_unset, 50U, "check returned arg");

  first_unset = 0;
  EXPECT_TRUE(bitmap.Get(2, 50, &first_unset), "get first half range");
  EXPECT_EQ(first_unset, 50U, "check returned arg");

  EXPECT_TRUE(bitmap.Get(80, 100, &first_unset), "get second half range");
  EXPECT_EQ(first_unset, 100U, "check returned arg");

  EXPECT_FALSE(bitmap.Get(50, 80, &first_unset), "get cleared range");
  EXPECT_EQ(first_unset, 50U, "check returned arg");
}

template <typename RawBitmap>
static void BoundaryArguments(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.Set(0, 0), ZX_OK, "range contains no bits");
  EXPECT_EQ(bitmap.Set(5, 4), ZX_ERR_INVALID_ARGS, "max is less than off");
  EXPECT_EQ(bitmap.Set(5, 5), ZX_OK, "range contains no bits");

  EXPECT_EQ(bitmap.Clear(0, 0), ZX_OK, "range contains no bits");
  EXPECT_EQ(bitmap.Clear(5, 4), ZX_ERR_INVALID_ARGS, "max is less than off");
  EXPECT_EQ(bitmap.Clear(5, 5), ZX_OK, "range contains no bits");

  EXPECT_TRUE(bitmap.Get(0, 0), "range contains no bits, so all are true");
  EXPECT_TRUE(bitmap.Get(5, 4), "range contains no bits, so all are true");
  EXPECT_TRUE(bitmap.Get(5, 5), "range contains no bits, so all are true");
}

template <typename RawBitmap>
static void SetOutOfOrder(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128U, "get size");

  EXPECT_EQ(bitmap.SetOne(0x64), ZX_OK, "setting later");
  EXPECT_EQ(bitmap.SetOne(0x60), ZX_OK, "setting earlier");

  EXPECT_TRUE(bitmap.GetOne(0x64), "getting first set");
  EXPECT_TRUE(bitmap.GetOne(0x60), "getting second set");
}

template <typename RawBitmap>
static void MoveConstructorTest(void) {
  RawBitmap src;
  EXPECT_EQ(src.Reset(128), ZX_OK);
  EXPECT_EQ(src.size(), 128U, "get size");
  EXPECT_EQ(src.SetOne(0x64), ZX_OK, "setting bit");
  EXPECT_TRUE(src.GetOne(0x64), "getting bit");

  RawBitmap target(std::move(src));
  EXPECT_TRUE(target.GetOne(0x64), "getting bit");
  EXPECT_EQ(src.Reset(0), ZX_OK, "we can still reset the moved-from object");
}

template <typename RawBitmap>
static void MoveAssignmentTest(void) {
  RawBitmap src;
  EXPECT_EQ(src.Reset(128), ZX_OK);
  EXPECT_EQ(src.size(), 128U, "get size");
  EXPECT_EQ(src.SetOne(0x64), ZX_OK, "setting bit");
  EXPECT_TRUE(src.GetOne(0x64), "getting bit");

  RawBitmap target = std::move(src);
  EXPECT_TRUE(target.GetOne(0x64), "getting bit");
  EXPECT_EQ(src.Reset(0), ZX_OK, "we can still reset the moved-from object");
}

template <typename RawBitmap>
static void GrowAcrossPage(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128u);

  EXPECT_FALSE(bitmap.GetOne(100));
  EXPECT_EQ(bitmap.SetOne(100), ZX_OK);
  EXPECT_TRUE(bitmap.GetOne(100));

  size_t bitoff_start;
  EXPECT_EQ(bitmap.Find(true, 101, 128, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "Expected tail end of bitmap to be unset");

  // We can't set bits out of range
  EXPECT_NE(bitmap.SetOne(16 * PAGE_SIZE - 1), ZX_OK);

  EXPECT_EQ(bitmap.Grow(16 * PAGE_SIZE), ZX_OK);
  EXPECT_EQ(bitmap.Find(true, 101, 16 * PAGE_SIZE, 1, &bitoff_start), ZX_ERR_NO_RESOURCES,
            "Expected tail end of bitmap to be unset");

  // Now we can set the previously inaccessible bits
  EXPECT_FALSE(bitmap.GetOne(16 * PAGE_SIZE - 1));
  EXPECT_EQ(bitmap.SetOne(16 * PAGE_SIZE - 1), ZX_OK);
  EXPECT_TRUE(bitmap.GetOne(16 * PAGE_SIZE - 1));

  // But our original 'set bit' is still set
  EXPECT_TRUE(bitmap.GetOne(100), "Growing should not unset bits");

  // If we shrink and re-expand the bitmap, it should
  // have cleared the underlying bits
  EXPECT_EQ(bitmap.Shrink(99), ZX_OK);
  EXPECT_EQ(bitmap.Grow(16 * PAGE_SIZE), ZX_OK);
  EXPECT_FALSE(bitmap.GetOne(100));
  EXPECT_FALSE(bitmap.GetOne(16 * PAGE_SIZE - 1));
}

template <typename RawBitmap>
static void GrowShrink(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128u);

  EXPECT_FALSE(bitmap.GetOne(100));
  EXPECT_EQ(bitmap.SetOne(100), ZX_OK);
  EXPECT_TRUE(bitmap.GetOne(100));

  for (size_t i = 8; i < 16; i++) {
    for (int j = -16; j <= 16; j++) {
      size_t bitmap_size = (1 << i) + j;

      for (size_t shrink_len = 1; shrink_len < 32; shrink_len++) {
        EXPECT_EQ(bitmap.Reset(bitmap_size), ZX_OK);
        EXPECT_EQ(bitmap.size(), bitmap_size);

        // This bit will be eliminated by shrink / grow
        EXPECT_FALSE(bitmap.GetOne(bitmap_size - shrink_len));
        EXPECT_EQ(bitmap.SetOne(bitmap_size - shrink_len), ZX_OK);
        EXPECT_TRUE(bitmap.GetOne(bitmap_size - shrink_len));

        // This bit will stay
        EXPECT_FALSE(bitmap.GetOne(bitmap_size - shrink_len - 1));
        EXPECT_EQ(bitmap.SetOne(bitmap_size - shrink_len - 1), ZX_OK);
        EXPECT_TRUE(bitmap.GetOne(bitmap_size - shrink_len - 1));

        EXPECT_EQ(bitmap.Shrink(bitmap_size - shrink_len), ZX_OK);
        EXPECT_EQ(bitmap.Grow(bitmap_size), ZX_OK);

        EXPECT_FALSE(bitmap.GetOne(bitmap_size - shrink_len), "Expected 'shrunk' bit to be unset");
        EXPECT_TRUE(bitmap.GetOne(bitmap_size - shrink_len - 1),
                    "Expected bit outside shrink range to be set");

        size_t bitoff_start;
        EXPECT_EQ(bitmap.Find(true, bitmap_size - shrink_len, bitmap_size, 1, &bitoff_start),
                  ZX_ERR_NO_RESOURCES, "Expected tail end of bitmap to be unset");
      }
    }
  }
}

template <typename RawBitmap>
static void GrowFailure(void) {
  RawBitmap bitmap;
  EXPECT_EQ(bitmap.Reset(128), ZX_OK);
  EXPECT_EQ(bitmap.size(), 128u);

  EXPECT_EQ(bitmap.Grow(64), ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(bitmap.Grow(128), ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(bitmap.Grow(128 + 1), ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(bitmap.Grow(8 * PAGE_SIZE), ZX_ERR_NO_RESOURCES);
}

#define TEMPLATIZED_TEST(test, specialization) \
  TEST(RawBitmapTests, test##_##specialization) { test<RawBitmapGeneric<specialization>>(); }

#define ALL_TESTS(specialization)                     \
  TEMPLATIZED_TEST(InitializedEmpty, specialization)  \
  TEMPLATIZED_TEST(SingleBit, specialization)         \
  TEMPLATIZED_TEST(SetTwice, specialization)          \
  TEMPLATIZED_TEST(ClearTwice, specialization)        \
  TEMPLATIZED_TEST(GetReturnArg, specialization)      \
  TEMPLATIZED_TEST(SetRange, specialization)          \
  TEMPLATIZED_TEST(FindSimple, specialization)        \
  TEMPLATIZED_TEST(ClearSubrange, specialization)     \
  TEMPLATIZED_TEST(BoundaryArguments, specialization) \
  TEMPLATIZED_TEST(ClearAll, specialization)          \
  TEMPLATIZED_TEST(SetOutOfOrder, specialization)

ALL_TESTS(DefaultStorage)
ALL_TESTS(VmoStorage)

TEMPLATIZED_TEST(MoveConstructorTest, VmoStorage)
TEMPLATIZED_TEST(MoveAssignmentTest, VmoStorage)
TEMPLATIZED_TEST(GrowAcrossPage, VmoStorage)
TEMPLATIZED_TEST(GrowShrink, VmoStorage)
TEMPLATIZED_TEST(GrowFailure, DefaultStorage)

}  // namespace tests
}  // namespace bitmap
