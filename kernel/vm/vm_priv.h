// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_VM_PRIV_H_
#define ZIRCON_KERNEL_VM_VM_PRIV_H_

#include <stdint.h>
#include <sys/types.h>

#include <kernel/mutex.h>
#include <kernel/range_check.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

// Individual files do `#define LOCAL_TRACE VM_GLOBAL_TRACE(0)`.
// This lets one edit just that one file to switch that `0` to `1`,
// or edit this file to replace `local` with `1` and get all those
// files at once.
#define VM_GLOBAL_TRACE(local) local

#endif  // ZIRCON_KERNEL_VM_VM_PRIV_H_
