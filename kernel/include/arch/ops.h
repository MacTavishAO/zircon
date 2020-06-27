// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_OPS_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_OPS_H_

/* #defines for the cache routines below */
#define ICACHE 1
#define DCACHE 2
#define UCACHE (ICACHE | DCACHE)

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <arch/defines.h>
#include <kernel/cpu.h>

__BEGIN_CDECLS

/* fast routines that most arches will implement inline */
static void arch_enable_ints(void);
static void arch_disable_ints(void);
static bool arch_ints_disabled(void);

static cpu_num_t arch_curr_cpu_num(void);
static uint arch_max_num_cpus(void);
static uint arch_cpu_features(void);

uint8_t arch_get_hw_breakpoint_count();
uint8_t arch_get_hw_watchpoint_count();

void arch_disable_cache(uint flags);
void arch_enable_cache(uint flags);

void arch_clean_cache_range(vaddr_t start, size_t len);
void arch_clean_invalidate_cache_range(vaddr_t start, size_t len);
void arch_invalidate_cache_range(vaddr_t start, size_t len);
void arch_sync_cache_range(vaddr_t start, size_t len);

/* Used to suspend work on a CPU until it is further shutdown.
 * This will only be invoked with interrupts disabled.  This function
 * must not re-enter the scheduler.
 * flush_done should be signaled after state is flushed. */
class Event;
void arch_flush_state_and_halt(Event *flush_done) __NO_RETURN;

int arch_idle_thread_routine(void *) __NO_RETURN;

/* arch optimized version of a page zero routine against a page aligned buffer */
void arch_zero_page(void *);

__END_CDECLS

/* give the specific arch a chance to override some routines */
#include <arch/arch_ops.h>

__BEGIN_CDECLS

/* The arch_blocking_disallowed() flag is used to check that in-kernel interrupt
 * handlers do not do any blocking operations.  This is a per-CPU flag.
 * Various blocking operations, such as mutex.Acquire(), contain assertions
 * that arch_blocking_disallowed() is false.
 *
 * arch_blocking_disallowed() should only be true when interrupts are
 * disabled. */
static inline bool arch_blocking_disallowed(void) {
  return READ_PERCPU_FIELD32(blocking_disallowed);
}

static inline void arch_set_blocking_disallowed(bool value) {
  WRITE_PERCPU_FIELD32(blocking_disallowed, value);
}

static inline uint32_t arch_num_spinlocks_held(void) { return READ_PERCPU_FIELD32(num_spinlocks); }

__END_CDECLS

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_OPS_H_
