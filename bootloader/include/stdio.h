// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_STDIO_H_
#define ZIRCON_BOOTLOADER_INCLUDE_STDIO_H_

#ifndef NULL
#define NULL ((void*)0)
#endif

#include <printf.h>
#include <stddef.h>
#include <stdint.h>

int puts16(char16_t* str);

#endif  // ZIRCON_BOOTLOADER_INCLUDE_STDIO_H_
