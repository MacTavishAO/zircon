// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INIT_INCLUDE_LK_INIT_H_
#define ZIRCON_KERNEL_LIB_INIT_INCLUDE_LK_INIT_H_

#include <lib/special-sections/special-sections.h>
#include <sys/types.h>

/*
 * LK's init system
 */

typedef void (*lk_init_hook)(uint level);

enum lk_init_level {
  LK_INIT_LEVEL_EARLIEST = 1,

  // Arch and platform specific init required to get system into a known state
  // and parsing the kernel command line.
  //
  // Most code should be deferred to later stages if possible, after the command
  // line is parsed and a debug UART is available.
  LK_INIT_LEVEL_ARCH_EARLY = 0x10000,
  LK_INIT_LEVEL_PLATFORM_EARLY = 0x20000,

  // Arch and platform specific code that needs to run prior to heap/virtual
  // memory being set up.
  //
  // The kernel command line and a UART is available, but no heap or VM.
  LK_INIT_LEVEL_ARCH_PREVM = 0x30000,
  LK_INIT_LEVEL_PLATFORM_PREVM = 0x40000,

  // Heap and VM initialization.
  LK_INIT_LEVEL_VM_PREHEAP = 0x50000,
  LK_INIT_LEVEL_HEAP = 0x60000,
  LK_INIT_LEVEL_VM = 0x70000,

  // Kernel and threading setup.
  LK_INIT_LEVEL_TOPOLOGY = 0x80000,
  LK_INIT_LEVEL_KERNEL = 0x90000,
  LK_INIT_LEVEL_THREADING = 0xa0000,

  // Arch and platform specific set up.
  //
  // Kernel heap, VM, and threads are available. Most init code should go
  // in these stages.
  LK_INIT_LEVEL_ARCH = 0xb0000,
  LK_INIT_LEVEL_PLATFORM = 0xc0000,
  LK_INIT_LEVEL_ARCH_LATE = 0xd0000,

  // Userspace started.
  LK_INIT_LEVEL_USER = 0xe0000,

  LK_INIT_LEVEL_LAST = UINT_MAX,
};

enum lk_init_flags {
  LK_INIT_FLAG_PRIMARY_CPU = 0x1,
  LK_INIT_FLAG_SECONDARY_CPUS = 0x2,
  LK_INIT_FLAG_ALL_CPUS = LK_INIT_FLAG_PRIMARY_CPU | LK_INIT_FLAG_SECONDARY_CPUS,
  LK_INIT_FLAG_CPU_SUSPEND = 0x4,
  LK_INIT_FLAG_CPU_RESUME = 0x8,
};

void lk_init_level(enum lk_init_flags flags, uint start_level, uint stop_level);

static inline void lk_primary_cpu_init_level(uint start_level, uint stop_level) {
  lk_init_level(LK_INIT_FLAG_PRIMARY_CPU, start_level, stop_level);
}

static inline void lk_init_level_all(enum lk_init_flags flags) {
  lk_init_level(flags, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_LAST);
}

struct lk_init_struct {
  uint level;
  uint flags;
  lk_init_hook hook;
  const char* name;
};

#define LK_INIT_HOOK_FLAGS(_name, _hook, _level, _flags)                                   \
  static const lk_init_struct _init_struct_##_name SPECIAL_SECTION(".data.rel.ro.lk_init", \
                                                                   lk_init_struct) = {     \
      .level = _level,                                                                     \
      .flags = _flags,                                                                     \
      .hook = _hook,                                                                       \
      .name = #_name,                                                                      \
  };

#define LK_INIT_HOOK(_name, _hook, _level) \
  LK_INIT_HOOK_FLAGS(_name, _hook, _level, LK_INIT_FLAG_PRIMARY_CPU)

#endif  // ZIRCON_KERNEL_LIB_INIT_INCLUDE_LK_INIT_H_
