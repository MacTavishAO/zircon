// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <arch.h>
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <platform.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/mmu.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mmu_mem_types.h>
#include <arch/x86/mp.h>
#include <arch/x86/proc_trace.h>
#include <lk/init.h>
#include <lk/main.h>
#include <vm/vm.h>

#include "arch/x86.h"

#define LOCAL_TRACE 0

#ifndef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16
#error "missing -mcx16"
#endif

/* save a pointer to the bootdata, if present */
void* _zbi_base;

void arch_early_init(void) { x86_mmu_early_init(); }

void arch_prevm_init(void) { x86_cpu_feature_init(); }

void arch_init(void) {
  const struct x86_model_info* model = x86_get_model();
  printf("Processor Model Info: type %#x family %#x model %#x stepping %#x\n",
         model->processor_type, model->family, model->model, model->stepping);
  printf("\tdisplay_family %#x display_model %#x\n", model->display_family, model->display_model);

  x86_feature_debug();

  x86_mmu_init();

  gdt_setup();
  idt_setup_readonly();

  x86_processor_trace_init();
}

void arch_late_init_percpu(void) {
  // Call per-CPU init function for the boot CPU.
  x86_cpu_feature_late_init_percpu();
}

void arch_setup_uspace_iframe(iframe_t* iframe, uintptr_t pc, uintptr_t sp, uintptr_t arg1,
                              uintptr_t arg2) {
  /* default user space flags:
   * IOPL 0
   * Interrupts enabled
   */
  iframe->flags = (0 << X86_FLAGS_IOPL_SHIFT) | X86_FLAGS_IF;

  iframe->cs = USER_CODE_64_SELECTOR;
  iframe->ip = pc;
  iframe->user_ss = USER_DATA_SELECTOR;
  iframe->user_sp = sp;

  iframe->rdi = arg1;
  iframe->rsi = arg2;
}

void arch_enter_uspace(iframe_t* iframe) {
  LTRACEF("entry %#" PRIxPTR " user stack %#" PRIxPTR "\n", iframe->ip, iframe->user_sp);
  LTRACEF("kernel stack %#" PRIxPTR "\n", x86_get_percpu()->default_tss.rsp0);

  arch_disable_ints();

  /* check that we're still pointed at the kernel gs */
  DEBUG_ASSERT(is_kernel_address(read_msr(X86_MSR_IA32_GS_BASE)));

  /* check that the kernel stack is set properly */
  DEBUG_ASSERT(is_kernel_address(x86_get_percpu()->default_tss.rsp0));

  x86_uspace_entry(iframe);
  __UNREACHABLE;
}

void arch_suspend(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  apic_io_save();
}

void arch_resume(void) {
  DEBUG_ASSERT(arch_ints_disabled());

  x86_init_percpu(0);
  x86_mmu_percpu_init();
  x86_pat_sync(cpu_num_to_mask(0));

  apic_local_init();

  // Ensure the CPU that resumed was assigned the correct percpu object.
  DEBUG_ASSERT(apic_local_id() == x86_get_percpu()->apic_id);

  apic_io_restore();
}

[[noreturn, gnu::noinline]] static void finish_secondary_entry(volatile int* aps_still_booting,
                                                               Thread* thread, uint cpu_num) {
  // Signal that this CPU is initialized.  It is important that after this
  // operation, we do not touch any resources associated with bootstrap
  // besides our Thread and stack, since this is the checkpoint the
  // bootstrap process uses to identify completion.
  int old_val = atomic_and(aps_still_booting, ~(1U << cpu_num));
  if (old_val == 0) {
    // If the value is already zero, then booting this CPU timed out.
    goto fail;
  }

  // Defer configuring memory settings until after the atomic_and above.
  // This ensures that we were in no-fill cache mode for the duration of early
  // AP init.
  DEBUG_ASSERT(x86_get_cr0() & X86_CR0_CD);
  x86_mmu_percpu_init();

  // Load the appropriate PAT/MTRRs.  This must happen after init_percpu, so
  // that this CPU is considered online.
  x86_pat_sync(1U << cpu_num);

  /* run early secondary cpu init routines up to the threading level */
  lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

  thread->SecondaryCpuInitEarly();
  // The thread stacks and struct are from a single allocation, free it
  // when we exit into the scheduler.
  thread->flags_ |= THREAD_FLAG_FREE_STRUCT;

  lk_secondary_cpu_entry();

// lk_secondary_cpu_entry only returns on an error, halt the core in this
// case.
fail:
  arch_disable_ints();
  while (1) {
    x86_hlt();
  }
}

// This is called from assembly, before any other C code.
// The %gs.base is not set up yet, so we have to trust that
// this function is simple enough that the compiler won't
// want to generate stack-protector prologue/epilogue code,
// which would use %gs.
__NO_SAFESTACK __NO_RETURN void x86_secondary_entry(volatile int* aps_still_booting,
                                                    Thread* thread) {
  // Would prefer this to be in init_percpu, but there is a dependency on a
  // page mapping existing, and the BP calls that before the VM subsystem is
  // initialized.
  apic_local_init();

  uint32_t local_apic_id = apic_local_id();
  int cpu_num = x86_apic_id_to_cpu_num(local_apic_id);
  if (cpu_num < 0) {
    // If we could not find our CPU number, do not proceed further
    arch_disable_ints();
    while (1) {
      x86_hlt();
    }
  }

  DEBUG_ASSERT(cpu_num > 0);

  // Set %gs.base to our percpu struct.  This has to be done before
  // calling x86_init_percpu, which initializes most of that struct, so
  // that x86_init_percpu can use safe-stack and/or stack-protector code.
  struct x86_percpu* const percpu = &ap_percpus[cpu_num - 1];
  write_msr(X86_MSR_IA32_GS_BASE, (uintptr_t)percpu);

  // Copy the stack-guard value from the boot CPU's perpcu.
  percpu->stack_guard = bp_percpu.stack_guard;

#if __has_feature(safe_stack)
  // Set up the initial unsafe stack pointer.
  DEBUG_ASSERT(IS_ALIGNED(thread->stack().unsafe_top(), 16));
  x86_write_gs_offset64(ZX_TLS_UNSAFE_SP_OFFSET, thread->stack().unsafe_top());
#endif

  x86_init_percpu((uint)cpu_num);

  // Now do the rest of the work, in a function that is free to
  // use %gs in its code.
  finish_secondary_entry(aps_still_booting, thread, cpu_num);
}

static int cmd_cpu(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s features\n", argv[0].str);
    printf("%s unplug <cpu_id>\n", argv[0].str);
    printf("%s hotplug <cpu_id>\n", argv[0].str);
    printf("%s rdmsr <cpu_id> <msr_id>\n", argv[0].str);
    printf("%s wdmsr <cpu_id> <msr_id> <value>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "features")) {
    x86_feature_debug();
  } else if (!strcmp(argv[1].str, "unplug")) {
    if (argc < 3) {
      printf("specify a cpu_id\n");
      goto usage;
    }
    zx_status_t status = mp_unplug_cpu((uint)argv[2].u);
    printf("CPU %lu unplugged: %d\n", argv[2].u, status);
  } else if (!strcmp(argv[1].str, "hotplug")) {
    if (argc < 3) {
      printf("specify a cpu_id\n");
      goto usage;
    }
    zx_status_t status = mp_hotplug_cpu((uint)argv[2].u);
    printf("CPU %lu hotplugged: %d\n", argv[2].u, status);
  } else if (!strcmp(argv[1].str, "rdmsr")) {
    if (argc != 4) {
      goto usage;
    }

    uint64_t val = read_msr_on_cpu((uint)argv[2].u, (uint)argv[3].u);
    printf("CPU %lu RDMSR %lxh val %lxh\n", argv[2].u, argv[3].u, val);
  } else if (!strcmp(argv[1].str, "wrmsr")) {
    if (argc != 5) {
      goto usage;
    }

    printf("CPU %lu WRMSR %lxh val %lxh\n", argv[2].u, argv[3].u, argv[4].u);
    write_msr_on_cpu((uint)argv[2].u, (uint)argv[3].u, argv[4].u);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("cpu", "cpu test commands", &cmd_cpu)
STATIC_COMMAND_END(cpu)
