// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace vmo_test {

zx::status<PhysVmo> GetTestPhysVmo(size_t size) {
  // We cannot create any physical VMOs without the root resource.
  if (!get_root_resource) {
    return zx::error_status(ZX_ERR_NOT_SUPPORTED);
  }

  // Fetch the address of the test reserved RAM region.  Even with the root
  // resource, we cannot use zx_vmo_create_physical to create a VMO which points
  // to RAM unless someone passed a kernel command line argument telling the
  // kernel to reserve a chunk of RAM for this purpose.
  //
  // If a chunk of RAM was reserved, the kernel will publish its size and
  // physical location in the kernel command line arguments.  If we have access
  // to the root resource, it is because we are running in the core-tests.zbi.
  // The kernel command line arguments should be available to us as environment
  // variables accessible via the stdlib getenv command.
  //
  // This is an all-or-nothing thing.  If we have the root resource, then we
  // should also have some RAM reserved for running these tests.  If we have the
  // root resource, but _don't_ have any reserved RAM, it should be considered a
  // test error.
  const char* const reserved_ram_info = getenv("kernel.ram.reserve.test");
  EXPECT_NOT_NULL(reserved_ram_info);
  if (reserved_ram_info == nullptr) {
    return zx::error_status(ZX_ERR_NO_RESOURCES);
  }

  // Parse the size and location.
  const char* const reserved_ram_info_end = reserved_ram_info + strlen(reserved_ram_info);
  const char* const comma = index(reserved_ram_info, ',');
  EXPECT_NOT_NULL(comma);
  if (!comma) {
    return zx::error_status(ZX_ERR_BAD_STATE);
  }

  PhysVmo ret;
  char* end;
  ret.size = strtoul(reserved_ram_info, &end, 0);
  EXPECT_EQ(comma, end);
  if (comma != end) {
    return zx::error_status(ZX_ERR_BAD_STATE);
  }

  ret.addr = strtoul(comma + 1, &end, 0);
  EXPECT_EQ(reserved_ram_info_end, end);
  if (reserved_ram_info_end != end) {
    return zx::error_status(ZX_ERR_BAD_STATE);
  }

  if (size > 0) {
    if (size > ret.size) {
      return zx::error_status(ZX_ERR_INVALID_ARGS);
    }
    ret.size = size;
  }

  // Go ahead and create the VMO itself.
  zx::unowned_resource root_res(get_root_resource());
  zx_status_t res = zx::vmo::create_physical(*root_res, ret.addr, ret.size, &ret.vmo);
  EXPECT_OK(res);
  if (res != ZX_OK) {
    return zx::error_status(res);
  }

  return zx::ok(std::move(ret));
}

}  // namespace vmo_test
