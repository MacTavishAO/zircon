// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_DRIVER_H_
#define ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_DRIVER_H_

#include <lib/special-sections/special-sections.h>
#include <stdint.h>

#include <lk/init.h>

typedef void (*lk_pdev_init_hook)(const void* driver_data, uint32_t length);

// for registering platform drivers
struct lk_pdev_init_struct {
  uint32_t type;           // driver type, as defined in <zircon/boot/kernel-drivers.h>
  lk_pdev_init_hook hook;  // hook for driver init
  uint level;              // init level for the hook
  const char* name;
};

#define LK_PDEV_INIT(_name, _type, _hook, _level)                            \
  static const lk_pdev_init_struct _dev_init_struct_##_name SPECIAL_SECTION( \
      ".data.rel.ro.lk_pdev_init", lk_pdev_init_struct) = {                  \
      .type = _type,                                                         \
      .hook = _hook,                                                         \
      .level = _level,                                                       \
      .name = #_name,                                                        \
  };

#endif  // ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_DRIVER_H_
