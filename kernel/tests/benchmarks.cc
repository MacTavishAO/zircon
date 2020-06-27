// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#include <arch/ops.h>
#include <kernel/brwlock.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <ktl/type_traits.h>

#include "tests.h"

const size_t BUFSIZE = (512 * 1024);  // must be smaller than max allowed heap allocation
const size_t ITER =
    (1UL * 1024 * 1024 * 1024 / BUFSIZE);  // enough iterations to have to copy/set 1GB of memory

__NO_INLINE static void bench_set_overhead() {
  uint32_t* buf = (uint32_t*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      __asm__ volatile("");
    }
    count = arch::Cycles() - count;
  }

  printf("took %" PRIu64 " cycles overhead to loop %zu times\n", count, ITER);

  free(buf);
}

__NO_INLINE static void bench_memset() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      memset(buf, 0, BUFSIZE);
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to memset a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_memset_per_page() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE; j += PAGE_SIZE) {
        memset(buf + j, 0, PAGE_SIZE);
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to per-page memset a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_zero_page() {
  uint8_t* buf = (uint8_t*)memalign(PAGE_SIZE, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: memalign failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE; j += PAGE_SIZE) {
        arch_zero_page(buf + j);
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to arch_zero_page a buffer of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

template <typename T>
__NO_INLINE static void bench_cset() {
  T* buf = (T*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE / sizeof(T); j++) {
        buf[j] = 0;
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to clear a buffer using wordsize %zu of size %zu %zu times "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, sizeof(*buf), BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000,
         bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_cset_wide() {
  uint32_t* buf = (uint32_t*)malloc(BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: malloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      for (size_t j = 0; j < BUFSIZE / sizeof(*buf) / 8; j++) {
        buf[j * 8] = 0;
        buf[j * 8 + 1] = 0;
        buf[j * 8 + 2] = 0;
        buf[j * 8 + 3] = 0;
        buf[j * 8 + 4] = 0;
        buf[j * 8 + 5] = 0;
        buf[j * 8 + 6] = 0;
        buf[j * 8 + 7] = 0;
      }
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to clear a buffer of size %zu %zu times 8 words at a time "
         "(%" PRIu64 " bytes), %" PRIu64 ".%03" PRIu64 " bytes/cycle\n",
         count, BUFSIZE, ITER, BUFSIZE * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_memcpy() {
  uint8_t* buf = (uint8_t*)calloc(1, BUFSIZE);
  if (buf == nullptr) {
    TRACEF("error: calloc failed\n");
    return;
  }

  uint64_t count;
  {
    InterruptDisableGuard irqd;

    count = arch::Cycles();
    for (size_t i = 0; i < ITER; i++) {
      memcpy(buf, buf + BUFSIZE / 2, BUFSIZE / 2);
    }
    count = arch::Cycles() - count;
  }

  uint64_t bytes_cycle = (BUFSIZE / 2 * ITER * 1000ULL) / count;
  printf("took %" PRIu64
         " cycles to memcpy a buffer of size %zu %zu times "
         "(%zu source bytes), %" PRIu64 ".%03" PRIu64 " source bytes/cycle\n",
         count, BUFSIZE / 2, ITER, BUFSIZE / 2 * ITER, bytes_cycle / 1000, bytes_cycle % 1000);

  free(buf);
}

__NO_INLINE static void bench_spinlock() {
  interrupt_saved_state_t state;
  SpinLock lock;
  uint64_t c;

#define COUNT (128 * 1024 * 1024)
  // test 1: acquire/release a spinlock with interrupts already disabled
  {
    InterruptDisableGuard irqd;

    c = arch::Cycles();
    for (size_t i = 0; i < COUNT; i++) {
      lock.Acquire();
      lock.Release();
    }
    c = arch::Cycles() - c;
  }

  printf("%" PRIu64 " cycles to acquire/release spinlock %d times (%" PRIu64 " cycles per)\n", c,
         COUNT, c / COUNT);

  // test 2: acquire/release a spinlock with irq save and irqs already disabled
  {
    InterruptDisableGuard irqd;

    c = arch::Cycles();
    for (size_t i = 0; i < COUNT; i++) {
      lock.AcquireIrqSave(state);
      lock.ReleaseIrqRestore(state);
    }
    c = arch::Cycles() - c;
  }

  printf("%" PRIu64
         " cycles to acquire/release spinlock w/irqsave (already disabled) %d times (%" PRIu64
         " cycles per)\n",
         c, COUNT, c / COUNT);

  // test 2: acquire/release a spinlock with irq save and irqs enabled
  c = arch::Cycles();
  for (size_t i = 0; i < COUNT; i++) {
    lock.AcquireIrqSave(state);
    lock.ReleaseIrqRestore(state);
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64 " cycles to acquire/release spinlock w/irqsave %d times (%" PRIu64
         " cycles per)\n",
         c, COUNT, c / COUNT);
#undef COUNT
}

__NO_INLINE static void bench_mutex() {
  Mutex m;

  static const uint count = 128 * 1024 * 1024;
  uint64_t c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    m.Acquire();
    m.Release();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64 " cycles to acquire/release uncontended mutex %u times (%" PRIu64
         " cycles per)\n",
         c, count, c / count);
}

template <typename LockType>
__NO_INLINE static void bench_rwlock() {
  LockType rw;
  static const uint count = 128 * 1024 * 1024;
  uint64_t c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    rw.ReadAcquire();
    rw.ReadRelease();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64
         " cycles to acquire/release uncontended brwlock(PI: %d) for read %u times (%" PRIu64
         " cycles per)\n",
         c, ktl::is_same_v<LockType, BrwLockPi>, count, c / count);

  c = arch::Cycles();
  for (size_t i = 0; i < count; i++) {
    rw.WriteAcquire();
    rw.WriteRelease();
  }
  c = arch::Cycles() - c;

  printf("%" PRIu64
         " cycles to acquire/release uncontended brwlock(PI: %d) for write %u times (%" PRIu64
         " cycles per)\n",
         c, ktl::is_same_v<LockType, BrwLockPi>, count, c / count);
}

int benchmarks(int, const cmd_args*, uint32_t) {
  bench_set_overhead();
  bench_memcpy();
  bench_memset();

  bench_memset_per_page();
  bench_zero_page();

  bench_cset<uint8_t>();
  bench_cset<uint16_t>();
  bench_cset<uint32_t>();
  bench_cset<uint64_t>();
  bench_cset_wide();

  bench_spinlock();
  bench_mutex();
  bench_rwlock<BrwLockPi>();
  bench_rwlock<BrwLockNoPi>();

  return 0;
}
