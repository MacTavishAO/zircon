#pragma once

#include <limits.h>
#include <stdatomic.h>
#include <zircon/syscalls.h>

#include "atomic.h"

void __wait(atomic_int* futex, atomic_int* waiters, int current_value);

/* Self-synchronized-destruction-safe lock functions */
#define UNLOCKED 0
#define LOCKED_NO_WAITERS 1
#define LOCKED_MAYBE_WAITERS 2

static inline void lock(atomic_int* l) {
  if (a_cas_shim(l, UNLOCKED, LOCKED_NO_WAITERS)) {
    a_cas_shim(l, LOCKED_NO_WAITERS, LOCKED_MAYBE_WAITERS);
    do
      __wait(l, UNLOCKED, LOCKED_MAYBE_WAITERS);
    while (a_cas_shim(l, UNLOCKED, LOCKED_MAYBE_WAITERS));
  }
}

static inline void unlock(atomic_int* l) {
  if (atomic_exchange(l, UNLOCKED) == LOCKED_MAYBE_WAITERS)
    _zx_futex_wake(l, 1);
}

static inline void unlock_requeue(atomic_int* l, zx_futex_t* r, zx_handle_t r_owner) {
  atomic_store(l, UNLOCKED);
  _zx_futex_requeue(l, /* wake count */ 0, /* l futex value */ UNLOCKED, r, /* requeue count */ 1,
                    /* requeue owner */ r_owner);
}
