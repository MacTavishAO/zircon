// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <new>

#include <fbl/mutex.h>
#include <zircon/assert.h>

#include "kernel.h"

// Gets a semaphore token.
//
// Inputs: sem = pointer to semaphore control block.
//         wait_opt = 0 (no wait), -1 (wait forever), or positive timeout count.
//
// Returns: 0 if successful, else -1 with errno set to error code.
int semPend(SEM sem, int wait_opt) __TA_ACQUIRE(sem) {
  ZX_DEBUG_ASSERT(wait_opt == WAIT_FOREVER);
  reinterpret_cast<fbl::Mutex*>(sem)->Acquire();
  return 0;
}

// Returns a semaphore token, ensures not already released.
void semPostBin(SEM sem) __TA_RELEASE(sem) { reinterpret_cast<fbl::Mutex*>(sem)->Release(); }

// Creates and initialize semaphore.
//
// Inputs: name = ASCII string of semaphore name.
//         count = initial semaphore count.
//         mode = task queuing mode: OS_FIFO (unused).
//
// Returns: ID of new semaphore, or NULL if error
SEM semCreate(const char name[8], int init_count, int mode) {
  ZX_DEBUG_ASSERT(init_count == 1);
  fbl::Mutex* mutex = new fbl::Mutex();
  SEM sem = reinterpret_cast<SEM>(mutex);
  return sem;
}

// Deletes specified semaphore, freeing its control block and any pending tasks.
void semDelete(SEM* semp) {
  fbl::Mutex* mutex = reinterpret_cast<fbl::Mutex*>(*semp);
  delete mutex;
  *semp = nullptr;
}
