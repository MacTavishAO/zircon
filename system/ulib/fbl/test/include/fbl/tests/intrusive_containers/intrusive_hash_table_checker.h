// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_HASH_TABLE_CHECKER_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_HASH_TABLE_CHECKER_H_

#include <fbl/intrusive_hash_table.h>
#include <fbl/tests/intrusive_containers/intrusive_doubly_linked_list_checker.h>
#include <fbl/tests/intrusive_containers/intrusive_singly_linked_list_checker.h>
#include <fbl/tests/intrusive_containers/test_environment_utils.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

// The hash table sanity checker implementation is shared across HashTables of
// all bucket types.
class HashTableChecker {
 public:
  template <typename ContainerType>
  static void SanityCheck(const ContainerType& container) {
    using BucketType = typename ContainerType::BucketType;
    using BucketChecker = typename BucketType::CheckerType;
    using HashType = typename ContainerType::HashType;
    using HashTraits = typename ContainerType::HashTraits;
    using KeyTraits = typename ContainerType::KeyTraits;

    // Demand that every bucket pass its sanity check.  Keep a running total
    // of the total size of the HashTable in the process.
    size_t total_size = 0;
    for (size_t i = 0; i < ContainerType::kNumBuckets; ++i) {
      ASSERT_NO_FATAL_FAILURES(BucketChecker::SanityCheck(container.buckets_[i]));
      total_size += SizeUtils<BucketType>::size(container.buckets_[i]);

      // For every element in the bucket, make sure that the bucket index
      // matches the hash of the element.
      for (const auto& obj : container.buckets_[i]) {
        ASSERT_EQ(HashTraits::GetHash(KeyTraits::GetKey(obj)), static_cast<HashType>(i));
      }
    }

    EXPECT_EQ(container.size(), total_size);
  }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_INTRUSIVE_HASH_TABLE_CHECKER_H_
