// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/block/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

namespace usb_virtual_bus {
namespace {

namespace usb_peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;

constexpr const char kManufacturer[] = "Google";
constexpr const char kProduct[] = "USB test drive";
constexpr const char kSerial[] = "ebfd5ad49d2a";

usb_peripheral::DeviceDescriptor GetDeviceDescriptor() {
  usb_peripheral::DeviceDescriptor device_desc = {};
  device_desc.bcdUSB = htole16(0x0200);
  device_desc.bDeviceClass = 0;
  device_desc.bDeviceSubClass = 0;
  device_desc.bDeviceProtocol = 0;
  device_desc.bMaxPacketSize0 = 64;
  // idVendor and idProduct are filled in later
  device_desc.bcdDevice = htole16(0x0100);
  // iManufacturer; iProduct and iSerialNumber are filled in later
  device_desc.bNumConfigurations = 1;

  device_desc.manufacturer = fidl::StringView(kManufacturer);
  device_desc.product = fidl::StringView(kProduct);
  device_desc.serial = fidl::StringView(kSerial);

  device_desc.idVendor = htole16(0x18D1);
  device_desc.idProduct = htole16(0xA021);
  return device_desc;
}

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus() {}

  // Initialize UMS. Asserts on failure.
  void InitUMS(fbl::String* devpath);
};

// Initialize UMS. Asserts on failure.
void USBVirtualBus::InitUMS(fbl::String* devpath) {
  auto device_desc = GetDeviceDescriptor();
  usb_peripheral::FunctionDescriptor ums_function_desc = {
      .interface_class = USB_CLASS_MSC,
      .interface_subclass = USB_SUBCLASS_MSC_SCSI,
      .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
  };

  std::vector<usb_peripheral::FunctionDescriptor> function_descs;
  function_descs.push_back(ums_function_desc);

  ASSERT_NO_FATAL_FAILURES(SetupPeripheralDevice(device_desc, std::move(function_descs)));

  fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), "class/block", O_RDONLY));
  while (fdio_watch_directory(fd.get(), WaitForAnyFile, ZX_TIME_INFINITE, devpath) != ZX_ERR_STOP) {
    continue;
  }
  *devpath = fbl::String::Concat({fbl::String("class/block/"), *devpath});
}

class BlockDeviceController {
 public:
  explicit BlockDeviceController(USBVirtualBus* bus) : bus_(bus) {}

  void Disconnect() {
    cachecontrol_.reset();
    ASSERT_NO_FATAL_FAILURES(bus_->ClearPeripheralDeviceFunctions());

    auto result2 = virtual_bus().Disconnect();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
  }

  void Connect() {
    auto device_desc = GetDeviceDescriptor();
    usb_peripheral::FunctionDescriptor ums_function_desc = {
        .interface_class = USB_CLASS_MSC,
        .interface_subclass = USB_SUBCLASS_MSC_SCSI,
        .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
    };

    std::vector<usb_peripheral::FunctionDescriptor> function_descs;
    function_descs.push_back(ums_function_desc);

    ASSERT_NO_FATAL_FAILURES(bus_->SetupPeripheralDevice(device_desc, std::move(function_descs)));

    fbl::String devpath;
    while (fdio_watch_directory(openat(bus_->GetRootFd(), "class/usb-cache-test", O_RDONLY),
                                WaitForAnyFile, ZX_TIME_INFINITE, &devpath) != ZX_ERR_STOP)
      continue;

    devpath = fbl::String::Concat({fbl::String("class/usb-cache-test/"), devpath});
    fbl::unique_fd fd(openat(bus_->GetRootFd(), devpath.c_str(), O_RDWR));
    zx::channel cache_control;
    ASSERT_OK(fdio_get_service_handle(fd.release(), cache_control.reset_and_get_address()));

    cachecontrol_.emplace(std::move(cache_control));
  }

  void EnableWritebackCache() {
    auto result = cachecontrol_->EnableWritebackCache();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result));
  }

  void DisableWritebackCache() {
    auto result = cachecontrol_->DisableWritebackCache();
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result));
  }

  void SetWritebackCacheReported(bool report) {
    auto result = cachecontrol_->SetWritebackCacheReported(report);
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result));
  }

 private:
  llcpp::fuchsia::hardware::usb::virtual_::bus::Bus::SyncClient& virtual_bus() {
    return bus_->virtual_bus();
  }
  usb_peripheral::Device::SyncClient& peripheral() { return bus_->peripheral(); }

  USBVirtualBus* bus_;
  std::optional<usb_peripheral::block::Device::SyncClient> cachecontrol_;
};

class UmsTest : public zxtest::Test {
 public:
  void SetUp() override;
  void TearDown() override;

 protected:
  USBVirtualBus bus_;
  fbl::String devpath_;
  zx::unowned_channel peripheral_;
  zx::unowned_channel virtual_bus_handle_;
  fbl::String GetTestdevPath() {
    // Open the block device
    // Special case for bad block mode. Need to enumerate the singleton block device.
    // NOTE: This MUST be a tight loop with NO sleeps in order to reproduce
    // the block-watcher deadlock. Changing the timing even slightly
    // makes this test invalid.
    while (true) {
      fbl::unique_fd fd(openat(bus_.GetRootFd(), "class/block", O_RDONLY));
      DIR* dir_handle = fdopendir(fd.get());
      fbl::AutoCall release_dir([=]() { closedir(dir_handle); });
      for (dirent* ent = readdir(dir_handle); ent; ent = readdir(dir_handle)) {
        if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
          last_known_devpath_ =
              fbl::String::Concat({fbl::String("class/block/"), fbl::String(ent->d_name)});
          return last_known_devpath_;
        }
      }
    }
  }

  // Waits for the block device to be removed
  // TODO (ZX-3385, ZX-3586) -- Use something better
  // than a busy loop.
  void WaitForRemove() {
    struct stat dirinfo;
    // NOTE: This MUST be a tight loop with NO sleeps in order to reproduce
    // the block-watcher deadlock. Changing the timing even slightly
    // makes this test invalid.
    while (!stat(last_known_devpath_.c_str(), &dirinfo))
      ;
  }

 private:
  fbl::String last_known_devpath_;
};

void UmsTest::SetUp() { ASSERT_NO_FATAL_FAILURES(bus_.InitUMS(&devpath_)); }

void UmsTest::TearDown() {
  ASSERT_NO_FATAL_FAILURES(bus_.ClearPeripheralDeviceFunctions());

  auto result2 = bus_.virtual_bus().Disable();
  ASSERT_NO_FATAL_FAILURES(ValidateResult(result2));
}

TEST_F(UmsTest, ReconnectTest) {
  // Disconnect and re-connect the block device 50 times as a sanity check
  // for race conditions and deadlocks.
  // If the test freezes; or something crashes at this point, it is likely
  // a regression in a driver (not a test flake).
  BlockDeviceController controller(&bus_);
  for (size_t i = 0; i < 50; i++) {
    ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
    WaitForRemove();
    ASSERT_NO_FATAL_FAILURES(controller.Connect());
    GetTestdevPath();
  }
  ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
}

TEST_F(UmsTest, CachedWriteWithNoFlushShouldBeDiscarded) {
  // Enable writeback caching on the block device
  BlockDeviceController controller(&bus_);
  ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
  ASSERT_NO_FATAL_FAILURES(controller.Connect());
  ASSERT_NO_FATAL_FAILURES(controller.SetWritebackCacheReported(true));
  ASSERT_NO_FATAL_FAILURES(controller.EnableWritebackCache());
  fbl::unique_fd fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));

  uint32_t blk_size;
  {
    fdio_cpp::UnownedFdioCaller caller(fd.get());
    auto result = ::llcpp::fuchsia::hardware::block::Block::Call::GetInfo(caller.channel());
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result));
    blk_size = result->info->block_size;
  }

  std::unique_ptr<uint8_t[]> write_buffer(new uint8_t[blk_size]);
  std::unique_ptr<uint8_t[]> read_buffer(new uint8_t[blk_size]);
  ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), read_buffer.get(), blk_size)));
  fd.reset(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
  // Create a pattern to write to the block device
  for (size_t i = 0; i < blk_size; i++) {
    write_buffer.get()[i] = static_cast<unsigned char>(i);
  }
  // Write the data to the block device
  ASSERT_EQ(blk_size, static_cast<uint64_t>(write(fd.get(), write_buffer.get(), blk_size)));
  ASSERT_EQ(-1, fsync(fd.get()));
  fd.reset();
  // Disconnect the block device without flushing the cache.
  // This will cause the data that was written to be discarded.
  ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
  ASSERT_NO_FATAL_FAILURES(controller.Connect());
  fd.reset(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
  ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), write_buffer.get(), blk_size)));
  ASSERT_NE(0, memcmp(read_buffer.get(), write_buffer.get(), blk_size));
}

TEST_F(UmsTest, UncachedWriteShouldBePersistedToBlockDevice) {
  BlockDeviceController controller(&bus_);
  // Disable writeback caching on the device
  ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
  ASSERT_NO_FATAL_FAILURES(controller.Connect());
  ASSERT_NO_FATAL_FAILURES(controller.SetWritebackCacheReported(false));
  ASSERT_NO_FATAL_FAILURES(controller.DisableWritebackCache());
  fbl::unique_fd fd(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));

  uint32_t blk_size;
  {
    fdio_cpp::UnownedFdioCaller caller(fd.get());
    auto result = ::llcpp::fuchsia::hardware::block::Block::Call::GetInfo(caller.channel());
    ASSERT_NO_FATAL_FAILURES(ValidateResult(result));
    blk_size = result->info->block_size;
  }

  // Allocate our buffer
  std::unique_ptr<uint8_t[]> write_buffer(new uint8_t[blk_size]);
  // Generate and write a pattern to the block device
  for (size_t i = 0; i < blk_size; i++) {
    write_buffer.get()[i] = static_cast<unsigned char>(i);
  }
  ASSERT_EQ(blk_size, static_cast<uint64_t>(write(fd.get(), write_buffer.get(), blk_size)));
  memset(write_buffer.get(), 0, blk_size);
  fd.reset();
  // Disconnect and re-connect the block device
  ASSERT_NO_FATAL_FAILURES(controller.Disconnect());
  ASSERT_NO_FATAL_FAILURES(controller.Connect());
  fd.reset(openat(bus_.GetRootFd(), GetTestdevPath().c_str(), O_RDWR));
  // Read back the pattern, which should match what was written
  // since writeback caching was disabled.
  ASSERT_EQ(blk_size, static_cast<uint64_t>(read(fd.get(), write_buffer.get(), blk_size)));
  for (size_t i = 0; i < blk_size; i++) {
    ASSERT_EQ(write_buffer.get()[i], static_cast<unsigned char>(i));
  }
}

TEST_F(UmsTest, BlkdevTest) {
  char errmsg[1024];
  fdio_spawn_action_t actions[1];
  actions[0] = {};
  actions[0].action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY;
  zx_handle_t fd_channel;
  ASSERT_OK(fdio_fd_clone(bus_.GetRootFd(), &fd_channel));
  actions[0].ns.handle = fd_channel;
  actions[0].ns.prefix = "/dev2";
  fbl::String path = fbl::String::Concat({fbl::String("/dev2/"), GetTestdevPath()});
  const char* argv[] = {"/boot/bin/blktest", "-d", path.c_str(), nullptr};
  zx_handle_t process;
  ASSERT_OK(fdio_spawn_etc(zx_job_default(), FDIO_SPAWN_CLONE_ALL, "/boot/bin/blktest", argv,
                           nullptr, 1, actions, &process, errmsg));
  uint32_t observed;
  zx_object_wait_one(process, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, &observed);
  zx_info_process_t proc_info;
  EXPECT_OK(zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr,
                               nullptr));
  EXPECT_EQ(proc_info.return_code, 0);
}

}  // namespace
}  // namespace usb_virtual_bus
