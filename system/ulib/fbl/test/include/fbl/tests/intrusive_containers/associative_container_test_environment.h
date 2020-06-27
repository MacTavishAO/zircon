// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_ASSOCIATIVE_CONTAINER_TEST_ENVIRONMENT_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_ASSOCIATIVE_CONTAINER_TEST_ENVIRONMENT_H_

#include <fbl/tests/intrusive_containers/base_test_environments.h>
#include <fbl/tests/lfsr.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

// AssociativeContainerTestEnvironment<>
//
// Test environment which defines and implements tests and test utilities which
// are applicable to all associative containers such as trees and hash-tables.
template <typename TestEnvTraits>
class AssociativeContainerTestEnvironment : public TestEnvironment<TestEnvTraits> {
 public:
  using ObjType = typename TestEnvTraits::ObjType;
  using PtrType = typename TestEnvTraits::PtrType;
  using ContainerTraits = typename ObjType::ContainerTraits;
  using ContainerType = typename ContainerTraits::ContainerType;
  using ContainerChecker = typename ContainerType::CheckerType;
  using OtherContainerType = typename ContainerTraits::OtherContainerType;
  using OtherContainerTraits = typename ContainerTraits::OtherContainerTraits;
  using PtrTraits = typename ContainerType::PtrTraits;
  using SpBase = TestEnvironmentSpecialized<TestEnvTraits>;
  using RefAction = typename TestEnvironment<TestEnvTraits>::RefAction;
  using KeyTraits = typename ContainerType::KeyTraits;
  using KeyType = typename ContainerType::KeyType;
  using OtherKeyType = typename OtherContainerType::KeyType;

  enum class PopulateMethod {
    AscendingKey,
    DescendingKey,
    RandomKey,
  };

  static constexpr KeyType kBannedKeyValue = 0xF00D;
  static constexpr OtherKeyType kBannedOtherKeyValue = 0xF00D;

  // Utility method for checking the size of the container via either size()
  // or size_slow(), depending on whether or not the container supports a
  // constant order size operation.
  template <typename CType>
  static size_t Size(const CType& container) {
    return SizeUtils<CType>::size(container);
  }

  void SetTestObjKeys(const PtrType& test_obj, PopulateMethod method) {
    ASSERT_NOT_NULL(test_obj);
    ASSERT_LT(test_obj->value(), OBJ_COUNT);

    // Assign a key to the object based on the chosen populate method.
    KeyType key = 0;
    OtherKeyType other_key = 0;

    switch (method) {
      case PopulateMethod::RandomKey:
        do {
          key = key_lfsr_.GetNext();
        } while (key == kBannedKeyValue);

        do {
          other_key = other_key_lfsr_.GetNext();
        } while (other_key == kBannedOtherKeyValue);
        break;

      case PopulateMethod::AscendingKey:
        key = test_obj->value();
        other_key = static_cast<OtherKeyType>(key + OBJ_COUNT);
        break;

      case PopulateMethod::DescendingKey:
        key = OBJ_COUNT - test_obj->value() - 1;
        other_key = static_cast<OtherKeyType>(key + OBJ_COUNT);
        break;
    }

    ZX_DEBUG_ASSERT(key != kBannedKeyValue);
    ZX_DEBUG_ASSERT(other_key != kBannedOtherKeyValue);

    // Set the primary key on the object.  Offset the "other" key by OBJ_COUNT
    test_obj->SetKey(key);
    OtherContainerTraits::SetKey(*test_obj, other_key);
  }

  void Populate(ContainerType& container, PopulateMethod method,
                RefAction ref_action = RefAction::HoldSome) {
    EXPECT_EQ(0U, ObjType::live_obj_count());

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, Size(container));

      // Unless explicitly told to do so, don't hold a reference in the
      // test environment for every 4th object created.  Note, this only
      // affects RefPtr tests.  Unmanaged pointers always hold an
      // unmanaged copy of the pointer (so it can be cleaned up), while
      // unique_ptr tests are not able to hold an extra copy of the
      // pointer (because it is unique)
      bool hold_ref;
      switch (ref_action) {
        case RefAction::HoldNone:
          hold_ref = false;
          break;
        case RefAction::HoldSome:
          hold_ref = (i & 0x3);
          break;
        case RefAction::HoldAll:
        default:
          hold_ref = true;
          break;
      }

      PtrType new_object = this->CreateTrackedObject(i, i, hold_ref);
      ASSERT_NOT_NULL(new_object);
      EXPECT_EQ(new_object->raw_ptr(), objects()[i]);

      ASSERT_NO_FAILURES(SetTestObjKeys(new_object, method));

      KeyType obj_key = KeyTraits::GetKey(*new_object);
      max_key_ = !i ? obj_key : KeyTraits::LessThan(max_key_, obj_key) ? obj_key : max_key_;

      // Alternate whether or not we move the pointer, or "transfer" it.
      // Transferring means different things for different pointer types.
      // For unmanaged, it just returns a reference to the pointer and
      // leaves the original unaltered.  For unique, it moves the pointer
      // (clearing the source).  For RefPtr, it makes a new RefPtr
      // instance, bumping the reference count in the process.
      if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
        container.insert(new_object);
#else
        container.insert(TestEnvTraits::Transfer(new_object));
#endif
        EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
      } else {
        container.insert(std::move(new_object));
        EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
      }
    }

    EXPECT_EQ(OBJ_COUNT, Size(container));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container));
  }

  void Populate(ContainerType& container, RefAction ref_action = RefAction::HoldSome) override {
    return Populate(container, PopulateMethod::AscendingKey, ref_action);
  }

  void DoInsertByKey(PopulateMethod populate_method) {
    ASSERT_NO_FATAL_FAILURES(Populate(container(), populate_method));
    ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::Reset());
  }

  void InsertByKey() {
    ASSERT_NO_FATAL_FAILURES(DoInsertByKey(PopulateMethod::AscendingKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT));

    ASSERT_NO_FATAL_FAILURES(DoInsertByKey(PopulateMethod::DescendingKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(2 * OBJ_COUNT));

    ASSERT_NO_FATAL_FAILURES(DoInsertByKey(PopulateMethod::RandomKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(3 * OBJ_COUNT));
  }

  void DoFindByKey(PopulateMethod populate_method) {
    ASSERT_NO_FATAL_FAILURES(Populate(container(), populate_method));

    // Lookup the various items which should be in the collection by key.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      KeyType key = objects()[i]->GetKey();
      size_t value = objects()[i]->value();

      auto iter = const_container().find(key);

      ASSERT_TRUE(iter.IsValid());
      EXPECT_EQ(key, iter->GetKey());
      EXPECT_EQ(value, iter->value());
    }

    // Fail to look up something which should not be in the collection.
    auto iter = const_container().find(kBannedKeyValue);
    EXPECT_FALSE(iter.IsValid());

    ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::Reset());
  }

  void FindByKey() {
    ASSERT_NO_FATAL_FAILURES(DoFindByKey(PopulateMethod::AscendingKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT));

    ASSERT_NO_FATAL_FAILURES(DoFindByKey(PopulateMethod::DescendingKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(2 * OBJ_COUNT));

    ASSERT_NO_FATAL_FAILURES(DoFindByKey(PopulateMethod::RandomKey));
    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(3 * OBJ_COUNT));
  }

  void DoEraseByKey(PopulateMethod populate_method, size_t already_erased) {
    ASSERT_NO_FATAL_FAILURES(Populate(container(), populate_method));
    size_t remaining = OBJ_COUNT;
    size_t erased = 0;

    // Fail to erase a key which is not in the container.
    EXPECT_NULL(container().erase(kBannedKeyValue));

    // Erase all of the even members of the collection by key.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      if (objects()[i] == nullptr)
        continue;

      KeyType key = objects()[i]->GetKey();
      if (key & 1)
        continue;

      ASSERT_NO_FATAL_FAILURES(
          TestEnvTraits::CheckCustomDeleteInvocations(erased + already_erased));
      ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::DoErase(key, i, remaining));
      ASSERT_NO_FATAL_FAILURES(
          TestEnvTraits::CheckCustomDeleteInvocations(++erased + already_erased));
      --remaining;
    }

    EXPECT_EQ(remaining, Size(container()));

    // Erase the remaining odd members.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      if (objects()[i] == nullptr)
        continue;

      KeyType key = objects()[i]->GetKey();
      EXPECT_TRUE(key & 1);

      ASSERT_NO_FATAL_FAILURES(
          TestEnvTraits::CheckCustomDeleteInvocations(erased + already_erased));
      ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::DoErase(key, i, remaining));
      ASSERT_NO_FATAL_FAILURES(
          TestEnvTraits::CheckCustomDeleteInvocations(++erased + already_erased));
      --remaining;
    }

    EXPECT_EQ(0u, Size(container()));

    ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::Reset());
  }

  void EraseByKey() {
    ASSERT_NO_FATAL_FAILURES(DoEraseByKey(PopulateMethod::AscendingKey, 0));
    ASSERT_NO_FATAL_FAILURES(DoEraseByKey(PopulateMethod::DescendingKey, OBJ_COUNT));
    ASSERT_NO_FATAL_FAILURES(DoEraseByKey(PopulateMethod::RandomKey, 2 * OBJ_COUNT));
  }

  void DoInsertOrFind(PopulateMethod populate_method, size_t already_destroyed) {
    for (unsigned int pass_iterator = 0u; pass_iterator < 2u; ++pass_iterator) {
      for (size_t i = 0u; i < OBJ_COUNT; ++i) {
        // Create a new tracked object.
        PtrType new_object = this->CreateTrackedObject(i, i, true);
        ASSERT_NOT_NULL(new_object);
        EXPECT_EQ(new_object->raw_ptr(), objects()[i]);
        ASSERT_NO_FAILURES(SetTestObjKeys(new_object, populate_method));

        // Insert the object into the container using insert_or_find.  There
        // should be no collision.  Exercise both the move and the copy
        // version of insert_or_find.
        typename ContainerType::iterator iter;
        bool success;

        if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
          success = container().insert_or_find(new_object, pass_iterator ? &iter : nullptr);
#else
          success = container().insert_or_find(TestEnvTraits::Transfer(new_object),
                                               pass_iterator ? &iter : nullptr);
#endif
          EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
        } else {
          success =
              container().insert_or_find(std::move(new_object), pass_iterator ? &iter : nullptr);

          EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
        }

        EXPECT_TRUE(success);

        // If we passed an iterator to the insert_or_find operation, it
        // should point to the newly inserted object.
        if (pass_iterator) {
          ASSERT_TRUE(iter.IsValid());
          EXPECT_EQ(objects()[i], iter->raw_ptr());
        }
      }

      // If we have not tested passing a non-null iterator yet, reset the
      // environment and do the test again.
      if (!pass_iterator) {
        ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::Reset());
      }
    }

    // The objects from the first test pass should have been deleted.
    ASSERT_NO_FATAL_FAILURES(
        TestEnvTraits::CheckCustomDeleteInvocations(already_destroyed + OBJ_COUNT));

    // Now go over the (populated) container and attempt to insert new
    // objects which have the same keys as existing objects.  Each of these
    // attempts should fail, but should find the objects which were inserted
    // previously.
    for (unsigned int pass_iterator = 0u; pass_iterator < 2u; ++pass_iterator) {
      for (size_t i = 0u; i < OBJ_COUNT; ++i) {
        ASSERT_NOT_NULL(objects()[i]);

        // Create a new non-tracked object; assign it the same key as
        // the existing object.
        PtrType new_object = TestEnvTraits::CreateObject(i);
        ASSERT_NOT_NULL(new_object);
        EXPECT_NE(new_object->raw_ptr(), objects()[i]);
        new_object->SetKey(KeyTraits::GetKey(*objects()[i]));

        // Attempt (but fail) to insert the object into the container
        // using insert_or_find.  There should be no collision.
        // Exercise both the move and the copy version of
        // insert_or_find.
        typename ContainerType::iterator iter;
        bool success;

        if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
          success = container().insert_or_find(new_object, pass_iterator ? &iter : nullptr);
#else
          success = container().insert_or_find(TestEnvTraits::Transfer(new_object),
                                               pass_iterator ? &iter : nullptr);
#endif
        } else {
          success =
              container().insert_or_find(std::move(new_object), pass_iterator ? &iter : nullptr);
        }

        // The object should not have been inserted.  If an attempt was
        // made to move the pointer into the collection, it should have
        // failed and we should still own the pointer.
        EXPECT_FALSE(success);
        ASSERT_NOT_NULL(new_object);

        // If we passed an iterator to the insert_or_find operation, it
        // should point to the object we collided with.
        if (pass_iterator) {
          EXPECT_TRUE(iter.IsValid());
          if (iter.IsValid()) {
            EXPECT_EQ(objects()[i], iter->raw_ptr());
            EXPECT_NE(PtrTraits::GetRaw(new_object), iter->raw_ptr());
            EXPECT_TRUE(
                KeyTraits::EqualTo(KeyTraits::GetKey(*iter), KeyTraits::GetKey(*new_object)));
          }
        }

        // Release the object we failed to insert.
        ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(
            already_destroyed + OBJ_COUNT + (OBJ_COUNT * pass_iterator) + i));
        TestEnvTraits::ReleaseObject(new_object);
        ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(
            already_destroyed + OBJ_COUNT + (OBJ_COUNT * pass_iterator) + i + 1));
      }
    }

    ASSERT_NO_FATAL_FAILURES(TestEnvironment<TestEnvTraits>::Reset());
    ASSERT_NO_FATAL_FAILURES(
        TestEnvTraits::CheckCustomDeleteInvocations(already_destroyed + (4 * OBJ_COUNT)));
  }

  void InsertOrFind() {
    // Each time we run this test, we create and destroy 4 * OBJ_COUNT objects
    ASSERT_NO_FATAL_FAILURES(DoInsertOrFind(PopulateMethod::AscendingKey, 0));
    ASSERT_NO_FATAL_FAILURES(DoInsertOrFind(PopulateMethod::DescendingKey, 4 * OBJ_COUNT));
    ASSERT_NO_FATAL_FAILURES(DoInsertOrFind(PopulateMethod::RandomKey, 8 * OBJ_COUNT));
  }

  template <typename CopyOrMoveUtil>
  void DoInsertOrReplace(size_t extra_elements, size_t already_destroyed) {
    ASSERT_EQ(0u, ObjType::live_obj_count());
    ASSERT_NO_FATAL_FAILURES(Populate(container(), PopulateMethod::AscendingKey));

    // Attempt to replace every element in the container with one that has
    // the same key.  Then attempt to replace some which were not in the
    // container to start with and verify that they were inserted instead.
    for (size_t i = 0; i < OBJ_COUNT + extra_elements; ++i) {
      PtrType new_obj = TestEnvTraits::CreateObject(i);
      ASSERT_NOT_NULL(new_obj);
      new_obj->SetKey(i);

      PtrType replaced = container().insert_or_replace(CopyOrMoveUtil::Op(new_obj));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));

      if (i < OBJ_COUNT) {
        EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());
        EXPECT_EQ(OBJ_COUNT, container().size());
        ASSERT_NOT_NULL(replaced);
        ASSERT_LT(replaced->value(), OBJ_COUNT);
        EXPECT_TRUE(KeyTraits::EqualTo(KeyTraits::GetKey(*replaced), i));
        EXPECT_TRUE(KeyTraits::EqualTo(KeyTraits::GetKey(*replaced), replaced->value()));

        ASSERT_EQ(objects()[i], PtrTraits::GetRaw(replaced));
        ReleaseObject(i);
        replaced = nullptr;
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
        EXPECT_EQ(OBJ_COUNT, container().size());

        // The replaced object should be gone now.
        ASSERT_NO_FATAL_FAILURES(
            TestEnvTraits::CheckCustomDeleteInvocations(already_destroyed + i + 1));
      } else {
        EXPECT_EQ(i + 1, ObjType::live_obj_count());
        EXPECT_EQ(i + 1, container().size());
        EXPECT_NULL(replaced);

        // We should have succeeded in inserting this object, so the delete count should not
        // have gone up.
        ASSERT_NO_FATAL_FAILURES(
            TestEnvTraits::CheckCustomDeleteInvocations(already_destroyed + OBJ_COUNT));
      }
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));

    while (!container().is_empty()) {
      PtrType ptr = container().erase(container().begin());
      TestEnvTraits::ReleaseObject(ptr);
    }

    ASSERT_NO_FATAL_FAILURES(TestEnvTraits::CheckCustomDeleteInvocations(
        already_destroyed + (2 * OBJ_COUNT) + extra_elements));
  }

  void InsertOrReplace() {
    // Each time we run each version of the tests, we create and destroy 2 *
    // OBJ_COUNT + the number of extra elements we specify;
    constexpr size_t EXTRA_ELEMENTS = 10;
    constexpr size_t TOTAL_OBJS = (2 * OBJ_COUNT) + EXTRA_ELEMENTS;

    ASSERT_NO_FATAL_FAILURES(DoInsertOrReplace<MoveUtil>(EXTRA_ELEMENTS, 0));
    if (CopyUtil<PtrTraits>::CanCopy) {
      ASSERT_NO_FATAL_FAILURES(DoInsertOrReplace<CopyUtil<PtrTraits>>(EXTRA_ELEMENTS, TOTAL_OBJS));
    }
  }

 protected:
  // Accessors for base class memebers so we don't have to type
  // this->base_member all of the time.
  using Sp = TestEnvironmentSpecialized<TestEnvTraits>;
  using Base = TestEnvironmentBase<TestEnvTraits>;
  static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;
  static constexpr size_t EVEN_OBJ_COUNT = (OBJ_COUNT >> 1) + (OBJ_COUNT & 1);
  static constexpr size_t ODD_OBJ_COUNT = (OBJ_COUNT >> 1);

  ContainerType& container() { return this->container_; }
  const ContainerType& const_container() { return this->container_; }
  ObjType** objects() { return this->objects_; }
  size_t& refs_held() { return this->refs_held_; }

  void ReleaseObject(size_t ndx) { Sp::ReleaseObject(ndx); }
  bool HoldingObject(size_t ndx) const { return Sp::HoldingObject(ndx); }

  Lfsr<KeyType> key_lfsr_ = Lfsr<KeyType>(0xa2328b73e343fd0f);
  Lfsr<OtherKeyType> other_key_lfsr_ = Lfsr<OtherKeyType>(0xbd5a2efcc5ba8344);
  KeyType max_key_ = 0u;

 private:
  // Notes about CopyUtil/MoveUtil.
  //
  // CopyUtil is a partially specialized trait template which acts as a helper
  // when we want to test both the copy and the move forms of an operation in
  // a (mostly) generic test.  It defines a single static operation which
  // returns a const PtrType& form of a pointer triggering the copy form of
  // an operation being tested when the test environment's pointer type
  // supports copying (eg, T* or RefPtr<T>).
  //
  // When copying is not supported (unique_ptr<T>), it will use std::move to
  // return an rvalue reference to the pointer instead.  This is *only* to
  // keep the compiler happy.  In general, tests should exercise themselves
  // using the MoveUtil helper, then test again using CopyUtil, but only if
  // CopyUtil::CanCopy is true.  Failure to check this before calling the test
  // again will simply result in the move version of the test being executed
  // twice (which is in-efficient, but not fatal).
  //
  // If/when if-constexpr becomes a real thing (C++17 is the hypothetical
  // target), we can eliminate the need to
  // use these partial specialization tricks and just rely on the compiler
  // eliminating the copy form of the test if the constexpr properties of the
  // pointer type indicate that it does not support copying.
  template <typename Traits, typename = void>
  struct CopyUtil;

  template <typename Traits>
  struct CopyUtil<Traits, typename std::enable_if<Traits::CanCopy == true>::type> {
    static constexpr bool CanCopy = Traits::CanCopy;
    static const PtrType& Op(PtrType& ptr) { return ptr; }
  };

  template <typename Traits>
  struct CopyUtil<Traits, typename std::enable_if<Traits::CanCopy == false>::type> {
    static constexpr bool CanCopy = Traits::CanCopy;
#if TEST_WILL_NOT_COMPILE || 0
    static const PtrType& Op(PtrType& ptr) { return ptr; }
#else
    static PtrType&& Op(PtrType& ptr) { return std::move(ptr); }
#endif
  };

  struct MoveUtil {
    static PtrType&& Op(PtrType& ptr) { return std::move(ptr); }
  };
};

// Explicit declaration of constexpr storage.
template <typename TestEnvTraits>
constexpr typename AssociativeContainerTestEnvironment<TestEnvTraits>::KeyType
    AssociativeContainerTestEnvironment<TestEnvTraits>::kBannedKeyValue;

template <typename TestEnvTraits>
constexpr typename AssociativeContainerTestEnvironment<TestEnvTraits>::OtherKeyType
    AssociativeContainerTestEnvironment<TestEnvTraits>::kBannedOtherKeyValue;

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_ASSOCIATIVE_CONTAINER_TEST_ENVIRONMENT_H_
