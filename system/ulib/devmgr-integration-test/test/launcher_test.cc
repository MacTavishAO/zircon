// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/vmo.h>
#include <zircon/device/vfs.h>

#include <zxtest/zxtest.h>

namespace devmgr_integration_test {

TEST(LauncherTest, DriverSearchPath) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

TEST(LauncherTest, LoadDrivers) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.load_drivers.push_back("/boot/driver/test.so");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

TEST(LauncherTest, Namespace) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/test_drivers");

  zx::channel bootfs_client, bootfs_server;
  ASSERT_OK(zx::channel::create(0, &bootfs_client, &bootfs_server));
  ASSERT_OK(fdio_open("/boot/driver", ZX_FS_RIGHT_READABLE |
                      ZX_FS_RIGHT_EXECUTABLE, bootfs_server.release()));

  args.flat_namespace.push_back(std::make_pair("/test_drivers", std::move(bootfs_client)));

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

TEST(LauncherTest, OutgoingServices) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));
  ASSERT_NE(devmgr.svc_root_dir().get(), ZX_HANDLE_INVALID);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Test we are able to connect to atleast one of the default services.
  const char* service = "svc/" fuchsia_device_manager_DebugDumper_Name;
  ASSERT_OK(fdio_service_connect_at(devmgr.svc_root_dir().get(), service, remote.release()));

  zx::vmo debug_vmo;
  zx_handle_t vmo_dup;
  size_t vmo_size = 512 * 512;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &debug_vmo));
  ASSERT_OK(zx_handle_duplicate(debug_vmo.get(), ZX_RIGHTS_IO | ZX_RIGHT_TRANSFER, &vmo_dup));
  zx_status_t call_status = ZX_OK;
  uint64_t data_written, data_avail;

  ASSERT_OK(fuchsia_device_manager_DebugDumperDumpTree(local.get(), vmo_dup, &call_status,
                                                       &data_written, &data_avail));
  ASSERT_OK(call_status);
}

}  // namespace devmgr_integration_test
