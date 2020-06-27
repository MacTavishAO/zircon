// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_

#include <lib/affine/ratio.h>

const affine::Ratio& rdtsc_to_nanos();

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_TIMER_H_
