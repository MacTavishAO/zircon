// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_FEATURE_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_FEATURE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/features.h>

#include <arch/arm64.h>

__BEGIN_CDECLS

enum arm64_microarch {
  UNKNOWN,

  ARM_CORTEX_A32,
  ARM_CORTEX_A35,
  ARM_CORTEX_A53,
  ARM_CORTEX_A55,
  ARM_CORTEX_A57,
  ARM_CORTEX_A65,
  ARM_CORTEX_A72,
  ARM_CORTEX_A73,
  ARM_CORTEX_A75,
  ARM_CORTEX_A76,
  ARM_CORTEX_A76AE,
  ARM_CORTEX_A77,
  ARM_CORTEX_A78,
  ARM_NEOVERSE_E1,
  ARM_NEOVERSE_N1,

  CAVIUM_CN88XX,
  CAVIUM_CN99XX,

  QEMU_TCG,
};

extern uint32_t arm64_isa_features;

enum arm64_microarch midr_to_microarch(uint32_t midr);

static inline bool arm64_feature_test(uint32_t feature) { return arm64_isa_features & feature; }

/* block size of the dc zva instruction, dcache cache line and icache cache line */
extern uint32_t arm64_zva_size;
extern uint32_t arm64_icache_size;
extern uint32_t arm64_dcache_size;

// call on every cpu to initialize the feature set
void arm64_feature_init(void);

// dump the feature set
void arm64_feature_debug(bool full);

void arm64_get_cache_info(arm64_cache_info_t* info);
void arm64_dump_cache_info(uint32_t cpu);

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_FEATURE_H_
