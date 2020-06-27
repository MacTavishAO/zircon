// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/time.h>

#include <kernel/mutex.h>
#include <lockdep/guard_multiple.h>
#include <lockdep/lockdep.h>

#include "tests.h"

#if WITH_LOCK_DEP_TESTS

// Defined in kernel/lib/lockdep.
zx_status_t TriggerAndWaitForLoopDetection(zx_time_t deadline);

namespace test {

// Global flag that determines whether try lock operations succeed.
bool g_try_lock_succeeds = true;

// Define some proxy types to simulate different kinds of locks.
struct Spinlock : Mutex {
  using Mutex::Mutex;

  bool AcquireIrqSave(uint64_t* flags) TA_ACQ() {
    (void)flags;
    Acquire();
    return true;
  }
  void ReleaseIrqRestore(uint64_t flags) TA_REL() {
    (void)flags;
    Release();
  }

  bool TryAcquire() TA_TRY_ACQ(true) {
    if (g_try_lock_succeeds)
      Acquire();
    return g_try_lock_succeeds;
  }
  bool TryAcquireIrqSave(uint64_t* flags) TA_TRY_ACQ(true) {
    (void)flags;
    if (g_try_lock_succeeds)
      Acquire();
    return g_try_lock_succeeds;
  }
};
LOCK_DEP_TRAITS(Spinlock, lockdep::LockFlagsIrqSafe);

// Fake C-style locking primitive.
struct spinlock_t {};
LOCK_DEP_TRAITS(spinlock_t, lockdep::LockFlagsIrqSafe);

void spinlock_lock(spinlock_t* /*lock*/) {}
void spinlock_unlock(spinlock_t* /*lock*/) {}
bool spinlock_try_lock(spinlock_t* /*lock*/) { return true; }
void spinlock_lock_irqsave(spinlock_t* /*lock*/, uint64_t* /*flags*/) {}
void spinlock_unlock_irqrestore(spinlock_t* /*lock*/, uint64_t /*flags*/) {}
bool spinlock_try_lock_irqsave(spinlock_t* /*lock*/, uint64_t* /*flags*/) { return true; }

// Type tags to select Guard<> lock policies for Spinlock and spinlock_t.
struct IrqSave {};
struct NoIrqSave {};
struct TryIrqSave {};
struct TryNoIrqSave {};

struct SpinlockNoIrqSave {
  struct State {};

  static bool Acquire(Spinlock* lock, State*) TA_ACQ(lock) {
    lock->Acquire();
    return true;
  }
  static void Release(Spinlock* lock, State*) TA_REL(lock) { lock->Release(); }
};
LOCK_DEP_POLICY_OPTION(Spinlock, NoIrqSave, SpinlockNoIrqSave);

struct SpinlockIrqSave {
  struct State {
    State() {}
    uint64_t flags;
  };

  static bool Acquire(Spinlock* lock, State* state) TA_ACQ(lock) {
    lock->AcquireIrqSave(&state->flags);
    return true;
  }
  static void Release(Spinlock* lock, State* state) TA_REL(lock) {
    lock->ReleaseIrqRestore(state->flags);
  }
};
LOCK_DEP_POLICY_OPTION(Spinlock, IrqSave, SpinlockIrqSave);

struct SpinlockTryNoIrqSave {
  struct State {};

  static bool Acquire(Spinlock* lock, State*) TA_TRY_ACQ(true, lock) { return lock->TryAcquire(); }
  static void Release(Spinlock* lock, State*) TA_REL(lock) { lock->Release(); }
};
LOCK_DEP_POLICY_OPTION(Spinlock, TryNoIrqSave, SpinlockTryNoIrqSave);

struct SpinlockTryIrqSave {
  struct State {
    State() {}
    uint64_t flags;
  };

  static bool Acquire(Spinlock* lock, State* state) TA_TRY_ACQ(true, lock) {
    return lock->TryAcquireIrqSave(&state->flags);
  }
  static void Release(Spinlock* lock, State* state) TA_REL(lock) {
    lock->ReleaseIrqRestore(state->flags);
  }
};
LOCK_DEP_POLICY_OPTION(Spinlock, TryIrqSave, SpinlockTryIrqSave);

struct spinlock_t_NoIrqSave {
  struct State {};

  static bool Acquire(spinlock_t* lock, State*) {
    spinlock_lock(lock);
    return true;
  }
  static void Release(spinlock_t* lock, State*) { spinlock_unlock(lock); }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, NoIrqSave, spinlock_t_NoIrqSave);

struct spinlock_t_IrqSave {
  struct State {
    State() {}
    uint64_t flags;
  };

  static bool Acquire(spinlock_t* lock, State* state) {
    spinlock_lock_irqsave(lock, &state->flags);
    return true;
  }
  static void Release(spinlock_t* lock, State* state) {
    spinlock_unlock_irqrestore(lock, state->flags);
  }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, IrqSave, spinlock_t_IrqSave);

struct spinlock_t_TryNoIrqSave {
  struct State {};

  static bool Acquire(spinlock_t* lock, State*) {
    spinlock_lock(lock);
    return g_try_lock_succeeds;
  }
  static void Release(spinlock_t* lock, State*) { spinlock_unlock(lock); }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, TryNoIrqSave, spinlock_t_TryNoIrqSave);

struct spinlock_t_TryIrqSave {
  struct State {
    State() {}
    uint64_t flags;
  };

  static bool Acquire(spinlock_t* lock, State* state) {
    spinlock_lock_irqsave(lock, &state->flags);
    return g_try_lock_succeeds;
  }
  static void Release(spinlock_t* lock, State* state) {
    spinlock_unlock_irqrestore(lock, state->flags);
  }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, TryIrqSave, spinlock_t_TryIrqSave);

struct Mutex : ::Mutex {
  using ::Mutex::Mutex;
};
// Uses the default traits: fbl::LockClassState::None.

struct Nestable : ::Mutex {
  using ::Mutex::Mutex;
};
LOCK_DEP_TRAITS(Nestable, lockdep::LockFlagsNestable);

struct TA_CAP("mutex") ReadWriteLock {
  bool AcquireWrite() TA_ACQ() { return true; }
  bool AcquireRead() TA_ACQ_SHARED() { return true; }
  void Release() TA_REL() {}

  struct Read {
    struct State {};
    struct Shared {};
    static bool Acquire(ReadWriteLock* lock, State*) TA_ACQ_SHARED(lock) {
      return lock->AcquireRead();
    }
    static void Release(ReadWriteLock* lock, State*) TA_REL(lock) { lock->Release(); }
  };

  struct Write {
    struct State {};
    static bool Acquire(ReadWriteLock* lock, State*) TA_ACQ(lock) { return lock->AcquireWrite(); }
    static void Release(ReadWriteLock* lock, State*) TA_REL(lock) { lock->Release(); }
  };
};
LOCK_DEP_POLICY_OPTION(ReadWriteLock, ReadWriteLock::Read, ReadWriteLock::Read);
LOCK_DEP_POLICY_OPTION(ReadWriteLock, ReadWriteLock::Write, ReadWriteLock::Write);

struct Foo {
  LOCK_DEP_INSTRUMENT(Foo, Mutex) lock;

  void TestRequire() TA_REQ(lock) {}
  void TestExclude() TA_EXCL(lock) {}
};

struct Bar {
  LOCK_DEP_INSTRUMENT(Bar, Mutex) lock;

  void TestRequire() TA_REQ(lock) {}
  void TestExclude() TA_EXCL(lock) {}
};

template <typename LockType, lockdep::LockFlags Flags = lockdep::LockFlagsNone>
struct Baz {
  LOCK_DEP_INSTRUMENT(Baz, LockType, Flags) lock;

  void TestRequire() TA_REQ(lock) {}
  void TestExclude() TA_EXCL(lock) {}
  void TestShared() TA_REQ_SHARED(lock) {}
};

struct MultipleLocks {
  LOCK_DEP_INSTRUMENT(MultipleLocks, Mutex) lock_a;
  LOCK_DEP_INSTRUMENT(MultipleLocks, Mutex) lock_b;

  void TestRequireLockA() TA_REQ(lock_a) {}
  void TestExcludeLockA() TA_EXCL(lock_a) {}
  void TestRequireLockB() TA_REQ(lock_b) {}
  void TestExcludeLockB() TA_EXCL(lock_b) {}
};

template <size_t Index>
struct Number {
  LOCK_DEP_INSTRUMENT(Number, Mutex) lock;

  void TestRequire() TA_REQ(lock) {}
  void TestExclude() TA_EXCL(lock) {}
};

lockdep::LockResult GetLastResult() {
#if WITH_LOCK_DEP
  lockdep::ThreadLockState* state = lockdep::ThreadLockState::Get();
  return state->last_result();
#else
  return lockdep::LockResult::Success;
#endif
}

void ResetTrackingState() {
#if WITH_LOCK_DEP
  for (auto& state : lockdep::LockClassState::Iter())
    state.Reset();
#endif
}

}  // namespace test

static bool lock_dep_dynamic_analysis_tests() {
  BEGIN_TEST;

  using lockdep::Guard;
  using lockdep::GuardMultiple;
  using lockdep::kInvalidLockClassId;
  using lockdep::LockClassState;
  using lockdep::LockFlagsMultiAcquire;
  using lockdep::LockFlagsNestable;
  using lockdep::LockFlagsReAcquireFatal;
  using lockdep::LockResult;
  using lockdep::ThreadLockState;
  using lockdep::kInvalidLockClassId;
  using test::Bar;
  using test::Baz;
  using test::Foo;
  using test::GetLastResult;
  using test::IrqSave;
  using test::MultipleLocks;
  using test::Mutex;
  using test::Nestable;
  using test::NoIrqSave;
  using test::Number;
  using test::ReadWriteLock;
  using test::Spinlock;
  using test::spinlock_t;
  using test::TryIrqSave;
  using test::TryNoIrqSave;

  // Reset the tracking state before each test run.
  test::ResetTrackingState();

  // Single lock.
  {
    Foo a{};

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());
  }

  // Single lock.
  {
    Bar a{};

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());
  }

  // Test order invariant.
  {
    Foo a{};
    Foo b{};

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());

    Guard<Mutex> guard_b{&b.lock};
    EXPECT_TRUE(guard_b);
    EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
  }

  // Test order invariant with a different lock class.
  {
    Bar a{};
    Bar b{};

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());

    Guard<Mutex> guard_b{&b.lock};
    EXPECT_TRUE(guard_b);
    EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
  }

  // Test address order invariant.
  {
    Foo a{};
    Foo b{};

    {
      GuardMultiple<2, Mutex> guard_all{&a.lock, &b.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      GuardMultiple<2, Mutex> guard_all{&b.lock, &a.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
  }

  // Test address order invariant with a different lock class.
  {
    Bar a{};
    Bar b{};

    {
      GuardMultiple<2, Mutex> guard_all{&a.lock, &b.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      GuardMultiple<2, Mutex> guard_all{&b.lock, &a.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
  }

  // Test address order invariant with spinlocks.
  {
    Baz<Spinlock> a{};
    Baz<Spinlock> b{};

    {
      GuardMultiple<2, Spinlock, NoIrqSave> guard_all{&a.lock, &b.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      GuardMultiple<2, Spinlock, NoIrqSave> guard_all{&b.lock, &a.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      test::g_try_lock_succeeds = true;
      GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&a.lock, &b.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      test::g_try_lock_succeeds = true;
      GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&b.lock, &a.lock};
      EXPECT_TRUE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      test::g_try_lock_succeeds = false;
      GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&a.lock, &b.lock};
      EXPECT_FALSE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      test::g_try_lock_succeeds = false;
      GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&b.lock, &a.lock};
      EXPECT_FALSE(guard_all);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
  }

  // Foo -> Bar -- establish order.
  {
    Foo a{};
    Bar b{};

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());

    Guard<Mutex> guard_b{&b.lock};
    EXPECT_TRUE(guard_b);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());
  }

  // Bar -> Foo -- check order invariant.
  {
    Foo a{};
    Bar b{};

    Guard<Mutex> guard_b{&b.lock};
    EXPECT_TRUE(guard_b);
    EXPECT_EQ(LockResult::Success, test::GetLastResult());

    Guard<Mutex> guard_a{&a.lock};
    EXPECT_TRUE(guard_a);
    EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult());
  }

  // Test external order invariant.
  {
    Baz<Nestable> baz1;
    Baz<Nestable> baz2;

    {
      Guard<Nestable> auto_baz1{&baz1.lock, 0};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Nestable> auto_baz2{&baz2.lock, 1};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Nestable> auto_baz2{&baz2.lock, 0};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Nestable> auto_baz1{&baz1.lock, 1};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Nestable> auto_baz2{&baz2.lock, 1};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Nestable> auto_baz1{&baz1.lock, 0};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::InvalidNesting, test::GetLastResult());
    }
  }

  // Test external order invariant with nestable flag supplied on the lock
  // member, rather than the lock type.
  {
    Baz<Mutex, LockFlagsNestable> baz1;
    Baz<Mutex, LockFlagsNestable> baz2;

    {
      Guard<Mutex> auto_baz1{&baz1.lock, 0};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> auto_baz2{&baz2.lock, 1};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Mutex> auto_baz2{&baz2.lock, 0};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> auto_baz1{&baz1.lock, 1};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Mutex> auto_baz2{&baz2.lock, 1};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> auto_baz1{&baz1.lock, 0};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::InvalidNesting, test::GetLastResult());
    }
  }

  // Test irq-safety invariant.
  {
    Baz<Mutex> baz1;
    Baz<Spinlock> baz2;

    {
      Guard<Mutex> auto_baz1{&baz1.lock};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Spinlock, NoIrqSave> auto_baz2{&baz2.lock};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Spinlock, NoIrqSave> auto_baz2{&baz2.lock};
      EXPECT_TRUE(auto_baz2);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> auto_baz1{&baz1.lock};
      EXPECT_TRUE(auto_baz1);
      EXPECT_EQ(LockResult::InvalidIrqSafety, test::GetLastResult());
    }
  }

  // Test spinlock options compile and basic guard functions.
  // TODO(eieio): Add Guard<>::state() accessor and check state values.
  {
    Baz<Spinlock> baz1;
    Baz<spinlock_t> baz2;

    {
      Guard<Spinlock, NoIrqSave> guard{&baz1.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<Spinlock, IrqSave> guard{&baz1.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<spinlock_t, NoIrqSave> guard{&baz2.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<spinlock_t, IrqSave> guard{&baz2.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      test::g_try_lock_succeeds = true;
      Guard<Spinlock, TryNoIrqSave> guard{&baz1.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      test::g_try_lock_succeeds = true;
      Guard<Spinlock, TryIrqSave> guard{&baz1.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      test::g_try_lock_succeeds = false;
      Guard<spinlock_t, TryNoIrqSave> guard{&baz2.lock};
      EXPECT_FALSE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      test::g_try_lock_succeeds = false;
      Guard<spinlock_t, TryIrqSave> guard{&baz2.lock};
      EXPECT_FALSE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    // Test that Guard<LockType, Option> fails to compile when Option is
    // required by the policy config but not specified.
    {
#if TEST_WILL_NOT_COMPILE || 0
      Guard<Spinlock> guard1{&baz1.lock};
      Guard<spinlock_t> guard2{&baz2.lock};
#endif
    }
  }

  // Test read/write lock compiles and basic guard options.
  {
    Baz<ReadWriteLock> a{};
    Baz<ReadWriteLock> b{};

    {
      Guard<ReadWriteLock, ReadWriteLock::Read> guard{&a.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Read> guard{&b.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Write> guard{&a.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Write> guard{&b.lock};
      EXPECT_TRUE(guard);
      guard.Release();
      EXPECT_FALSE(guard);
    }
  }

  // Test read/write lock order invariants.
  {
    Baz<ReadWriteLock> a{};
    Baz<ReadWriteLock> b{};

    {
      Guard<ReadWriteLock, ReadWriteLock::Read> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<ReadWriteLock, ReadWriteLock::Read> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Read> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<ReadWriteLock, ReadWriteLock::Write> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Write> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<ReadWriteLock, ReadWriteLock::Read> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
    }

    {
      Guard<ReadWriteLock, ReadWriteLock::Write> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<ReadWriteLock, ReadWriteLock::Write> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult());
    }

    {
      GuardMultiple<2, ReadWriteLock, ReadWriteLock::Read> guard{&a.lock, &b.lock};
      EXPECT_TRUE(guard);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      GuardMultiple<2, ReadWriteLock, ReadWriteLock::Write> guard{&a.lock, &b.lock};
      EXPECT_TRUE(guard);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
  }

  // Test that each lock in a structure behaves as an individual lock class.
  {
    MultipleLocks value{};

    {
      Guard<Mutex> guard_a{&value.lock_a};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&value.lock_b};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    {
      Guard<Mutex> guard_b{&value.lock_b};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_a{&value.lock_a};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult());
    }
  }

  // Test multi-acquire rule. Re-acquiring the same lock class is allowed,
  // however, ordering with other locks is still enforced.
  {
    Baz<Mutex, LockFlagsMultiAcquire> a;
    Baz<Mutex, LockFlagsMultiAcquire> b;
#if 0 || TEST_WILL_NOT_COMPILE
    // Test mutually exclusive flags fail to compile.
    Baz<Mutex, LockFlagsMultiAcquire | LockFlagsNestable> c;
    Baz<Mutex, LockFlagsMultiAcquire | LockFlagsReAcquireFatal> d;
#endif

    // Use a unique lock class for each of these order tests.
    Foo before;
    Bar after;
    Baz<Mutex> between;

    // Test re-acquiring the same lock class.
    {
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // Test ordering with another lock class before this one.
    {
      Guard<Mutex> guard_before{&before.lock};
      EXPECT_TRUE(guard_before);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
    {
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_before{&before.lock};
      EXPECT_TRUE(guard_before);
      EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult());
    }
    {
      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      // Subsequent violations are not reported.
      Guard<Mutex> guard_before{&before.lock};
      EXPECT_TRUE(guard_before);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // Test ordering with another lock class after this one.
    {
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_after{&after.lock};
      EXPECT_TRUE(guard_after);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
    {
      Guard<Mutex> guard_after{&after.lock};
      EXPECT_TRUE(guard_after);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult());
    }
    {
      Guard<Mutex> guard_after{&after.lock};
      EXPECT_TRUE(guard_after);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      // Subsequent violations are not reported.
      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // Test ordering with another lock class between this one.
    {
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_between{&between.lock};
      EXPECT_TRUE(guard_between);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult());
    }
    {
      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_between{&between.lock};
      EXPECT_TRUE(guard_between);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      // Subsequent violations are not reported.
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }
  }

  // Test circular dependency detection.
  {
    Number<1> a{};  // Node A.
    Number<2> b{};  // Node B.
    Number<3> c{};  // Node C.
    Number<4> d{};  // Node D.

    // A -> B
    {
      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // B -> C
    {
      Guard<Mutex> guard_b{&b.lock};
      EXPECT_TRUE(guard_b);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_c{&c.lock};
      EXPECT_TRUE(guard_c);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // C -> A -- cycle in (A, B, C)
    {
      Guard<Mutex> guard_c{&c.lock};
      EXPECT_TRUE(guard_c);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // C -> D
    {
      Guard<Mutex> guard_c{&c.lock};
      EXPECT_TRUE(guard_c);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_d{&d.lock};
      EXPECT_TRUE(guard_d);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // D -> A -- cycle in (A, B, C, D)
    {
      Guard<Mutex> guard_d{&d.lock};
      EXPECT_TRUE(guard_d);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());

      Guard<Mutex> guard_a{&a.lock};
      EXPECT_TRUE(guard_a);
      EXPECT_EQ(LockResult::Success, test::GetLastResult());
    }

    // Ensure that the loop detection pass completes before the test ends to
    // avoid triggering lockdep failures in CQ/CI. Use an infinite timeout and
    // let test infra kill the test due to timeout instead.
    zx_status_t status = TriggerAndWaitForLoopDetection(ZX_TIME_INFINITE);
    EXPECT_EQ(ZX_OK, status);
  }


  // Reset the tracking state to ensure that circular dependencies are not
  // reported outside of the test.
  test::ResetTrackingState();

  END_TEST;
}

// Basic compile-time tests of lockdep clang lock annotations.
static bool lock_dep_static_analysis_tests() {
  BEGIN_TEST;

  using lockdep::Guard;
  using lockdep::GuardMultiple;
  using lockdep::LockResult;
  using lockdep::ThreadLockState;
  using test::Bar;
  using test::Baz;
  using test::Foo;
  using test::MultipleLocks;
  using test::Mutex;
  using test::Nestable;
  using test::Number;
  using test::ReadWriteLock;
  using test::Spinlock;
  using test::TryNoIrqSave;

  // Test require and exclude annotations.
  {
    Foo a{};

    Guard<Mutex> guard_a{&a.lock};
    a.TestRequire();
#if TEST_WILL_NOT_COMPILE || 0
    a.TestExclude();
#endif

    guard_a.Release();
#if TEST_WILL_NOT_COMPILE || 0
    a.TestRequire();
#endif
    a.TestExclude();
  }

  // Test multiple acquire.
  {
    Foo a{};

    Guard<Mutex> guard_a{&a.lock};
#if TEST_WILL_NOT_COMPILE || 0
    Guard<Mutex> guard_b{&a.lock};
#endif
  }

  // Test sequential acquire/release.
  {
    Foo a{};

    Guard<Mutex> guard_a{&a.lock};
    guard_a.Release();
    Guard<Mutex> guard_b{&a.lock};
  }

  // Test shared.
  {
    Baz<ReadWriteLock> a{};

    Guard<ReadWriteLock, ReadWriteLock::Read> guard_a{&a.lock};
    a.TestShared();
#if TEST_WILL_NOT_COMPILE || 0
    a.TestRequire();
#endif
  }

  {
    Baz<ReadWriteLock> a{};

    Guard<ReadWriteLock, ReadWriteLock::Write> guard_a{&a.lock};
    a.TestShared();
    a.TestRequire();
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(lock_dep_tests)
UNITTEST("lock_dep_dynamic_analysis_tests", lock_dep_dynamic_analysis_tests)
UNITTEST("lock_dep_static_analysis_tests", lock_dep_static_analysis_tests)
UNITTEST_END_TESTCASE(lock_dep_tests, "lock_dep_tests", "lock_dep_tests")

#endif
