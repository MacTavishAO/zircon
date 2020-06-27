// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ENDIAN_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ENDIAN_H_

#include <sys/types.h>

#ifndef __BYTE_ORDER__
#error Compiler does not provide __BYTE_ORDER__
#endif

/* the compiler provides it, use what it says */
#define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#define BYTE_ORDER __BYTE_ORDER__

// define functions that unconditionally swap
static inline uint64_t SWAP_64(uint64_t x) { return __builtin_bswap64(x); }

static inline uint32_t SWAP_32(uint32_t x) { return __builtin_bswap32(x); }

static inline uint16_t SWAP_16(uint16_t x) { return __builtin_bswap16(x); }

// standard swap macros
#if BYTE_ORDER == BIG_ENDIAN
#define LE64(val) SWAP_64(val)
#define LE32(val) SWAP_32(val)
#define LE16(val) SWAP_16(val)
#define BE64(val) (val)
#define BE32(val) (val)
#define BE16(val) (val)
#else
#define LE64(val) (val)
#define LE32(val) (val)
#define LE16(val) (val)
#define BE64(val) SWAP_64(val)
#define BE32(val) SWAP_32(val)
#define BE16(val) SWAP_16(val)
#endif

#define LE32SWAP(var) (var) = LE32(var);
#define LE16SWAP(var) (var) = LE16(var);
#define BE32SWAP(var) (var) = BE32(var);
#define BE16SWAP(var) (var) = BE16(var);

/* classic network byte swap stuff */
#define ntohs(n) BE16(n)
#define htons(h) BE16(h)
#define ntohl(n) BE32(n)
#define htonl(h) BE32(h)

/* 64-bit network byte swap stuff */
#define htobe64(h) BE64(h)
#define be64toh(b) BE64(b)

// some memory access macros
#if __POWERPC__
#include <ppc_intrinsics.h>

#define READ_MEM_WORD(ptr) __lwbrx((word *)(ptr), 0)
#define READ_MEM_HALFWORD(ptr) __lhbrx((halfword *)(ptr), 0)
#define READ_MEM_BYTE(ptr) (*(byte *)(ptr))
#define WRITE_MEM_WORD(ptr, data) __stwbrx(data, (word *)(ptr), 0)
#define WRITE_MEM_HALFWORD(ptr, data) __sthbrx(data, (halfword *)(ptr), 0)
#define WRITE_MEM_BYTE(ptr, data) (*(byte *)(ptr) = (data))
#else
#define READ_MEM_WORD(ptr) SWAPIT_32(*(word *)(ptr))
#define READ_MEM_HALFWORD(ptr) SWAPIT_16(*(halfword *)(ptr))
#define READ_MEM_BYTE(ptr) (*(byte *)(ptr))
#define WRITE_MEM_WORD(ptr, data) (*(word *)(ptr) = SWAPIT_32(data))
#define WRITE_MEM_HALFWORD(ptr, data) (*(halfword *)(ptr) = SWAPIT_16(data))
#define WRITE_MEM_BYTE(ptr, data) (*(byte *)(ptr) = (data))
#endif

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_ENDIAN_H_
