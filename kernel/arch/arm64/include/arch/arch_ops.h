// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_OPS_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_OPS_H_

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <zircon/compiler.h>

#include <arch/arm64.h>
#include <arch/arm64/feature.h>
#include <arch/arm64/interrupt.h>
#include <arch/arm64/mp.h>

__BEGIN_CDECLS

#define ENABLE_CYCLE_COUNTER 1

static inline uint32_t arch_cpu_features(void) { return arm64_isa_features; }

static inline uint32_t arch_dcache_line_size(void) { return arm64_dcache_size; }

static inline uint32_t arch_icache_line_size(void) { return arm64_icache_size; }

// Log architecture-specific data for process creation.
// This can only be called after the process has been created and before
// it is running. Alas we can't use zx_koid_t here as the arch layer is at a
// lower level than zircon.
static inline void arch_trace_process_create(uint64_t pid, paddr_t tt_phys) {
  // nothing to do
}

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_OPS_H_
