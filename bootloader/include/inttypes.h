// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_INTTYPES_H_
#define ZIRCON_BOOTLOADER_INCLUDE_INTTYPES_H_

#include <stdint.h>

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"

#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"

#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"

#ifdef __clang__
#define PRIx64 "llx"
#define PRIu64 "llu"
#else
#define PRIx64 "lx"
#define PRIu64 "lu"
#endif

#endif  // ZIRCON_BOOTLOADER_INCLUDE_INTTYPES_H_
