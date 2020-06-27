// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PLATFORM_ACCESS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PLATFORM_ACCESS_H_

#include <sys/types.h>

#include <arch/x86.h>

// Lightweight class to wrap MSR (x86 Model Specific Register) accesses.
//
// MSR access functions are virtual; a test can pass a fake or mock accessor to intercept
// read_msr/write_msr.
class MsrAccess {
 public:
  virtual uint64_t read_msr(uint32_t msr_index) { return ::read_msr(msr_index); }

  virtual void write_msr(uint32_t msr_index, uint64_t value) { ::write_msr(msr_index, value); }
};

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PLATFORM_ACCESS_H_
