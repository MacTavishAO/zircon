// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>

namespace ramdevice_client {

__EXPORT
zx_status_t RamNandCtl::Create(fbl::RefPtr<RamNandCtl>* out) {
  driver_integration_test::IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = true;
  // TODO(surajmalhotra): Remove creation of isolated devmgr from this lib so that caller can choose
  // their creation parameters.
  args.board_name = "astro";

  driver_integration_test::IsolatedDevmgr devmgr;
  zx_status_t st = driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr);
  if (st != ZX_OK) {
    fprintf(stderr, "Could not create ram_nand_ctl device, %d\n", st);
    return st;
  }

  fbl::unique_fd ctl;
  st = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "misc/nand-ctl", &ctl);
  if (st != ZX_OK) {
    fprintf(stderr, "ram_nand_ctl device failed enumerated, %d\n", st);
    return st;
  }

  *out = fbl::AdoptRef(new RamNandCtl(std::move(devmgr), std::move(ctl)));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNand::Create(const fuchsia_hardware_nand_RamNandInfo* config,
                            std::optional<RamNand>* out) {
  fbl::unique_fd control(open(kBasePath, O_RDWR));

  zx::channel ctl_svc;
  zx_status_t st = fdio_get_service_handle(control.release(), ctl_svc.reset_and_get_address());

  char name[fuchsia_hardware_nand_NAME_LEN + 1];
  size_t out_name_size;
  zx_status_t status;
  st = fuchsia_hardware_nand_RamNandCtlCreateDevice(ctl_svc.get(), config, &status, name,
                                                    fuchsia_hardware_nand_NAME_LEN, &out_name_size);
  if (st != ZX_OK || status != ZX_OK) {
    st = st != ZX_OK ? st : status;
    fprintf(stderr, "Could not create ram_nand device, %d\n", st);
    return st;
  }
  name[out_name_size] = '\0';
  fbl::StringBuffer<PATH_MAX> path;
  path.Append(kBasePath);
  path.Append("/");
  path.Append(name);

  fbl::unique_fd ram_nand(open(path.c_str(), O_RDWR));
  if (!ram_nand) {
    fprintf(stderr, "Could not open ram_nand\n");
    return ZX_ERR_INTERNAL;
  }

  *out = RamNand(std::move(ram_nand), path.ToString(), fbl::String(name));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNand::Create(fbl::RefPtr<RamNandCtl> ctl,
                            const fuchsia_hardware_nand_RamNandInfo* config,
                            std::optional<RamNand>* out) {
  fdio_t* io = fdio_unsafe_fd_to_io(ctl->fd().get());
  if (io == NULL) {
    fprintf(stderr, "Could not get fdio object\n");
    return ZX_ERR_INTERNAL;
  }
  zx_handle_t ctl_svc = fdio_unsafe_borrow_channel(io);

  char name[fuchsia_hardware_nand_NAME_LEN + 1];
  size_t out_name_size;
  zx_status_t status;
  zx_status_t st = fuchsia_hardware_nand_RamNandCtlCreateDevice(
      ctl_svc, config, &status, name, fuchsia_hardware_nand_NAME_LEN, &out_name_size);
  fdio_unsafe_release(io);
  if (st != ZX_OK || status != ZX_OK) {
    st = st != ZX_OK ? st : status;
    fprintf(stderr, "Could not create ram_nand device, %d\n", st);
    return st;
  }
  name[out_name_size] = '\0';

  // TODO(ZX-3193): We should be able to open relative to ctl->fd(), but
  // due to a bug, we have to be relative to devfs_root instead.
  fbl::StringBuffer<PATH_MAX> path;
  path.Append("misc/nand-ctl/");
  path.Append(name);
  fprintf(stderr, "Trying to open (%s)\n", path.c_str());

  fbl::unique_fd fd;
  st = devmgr_integration_test::RecursiveWaitForFile(ctl->devfs_root(), path.c_str(), &fd);
  if (st != ZX_OK) {
    return st;
  }

  *out = RamNand(std::move(fd), std::move(ctl));
  return ZX_OK;
}

__EXPORT
zx_status_t RamNand::CreateIsolated(const fuchsia_hardware_nand_RamNandInfo* config,
                                    std::optional<RamNand>* out) {
  fbl::RefPtr<RamNandCtl> ctl;
  zx_status_t st = RamNandCtl::Create(&ctl);
  if (st != ZX_OK) {
    return st;
  }
  return Create(std::move(ctl), config, out);
}

__EXPORT
RamNand::~RamNand() {
  if (unbind && fd_) {
    zx::channel dev;
    zx_status_t status = fdio_get_service_handle(fd_.release(), dev.reset_and_get_address());
    if (status != ZX_OK) {
      fprintf(stderr, "Could not get service handle when unbinding ram_nand, %d\n", status);
      return;
    }
    zx_status_t call_status = ZX_OK;
    auto resp =
        ::llcpp::fuchsia::device::Controller::Call::ScheduleUnbind(zx::unowned_channel(dev.get()));
    status = resp.status();
    if (resp->result.is_err()) {
      call_status = resp->result.err();
    }
    if (status == ZX_OK) {
      status = call_status;
    }
    if (status != ZX_OK) {
      fprintf(stderr, "Could not unbind ram_nand, %d\n", status);
    }
  }
}

}  // namespace ramdevice_client
