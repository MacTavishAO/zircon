// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fs/vfs_types.h>
#include <fs/vmo_file.h>
#include <zxtest/zxtest.h>

namespace {

using VnodeOptions = fs::VnodeConnectionOptions;
using VnodeInfo = fs::VnodeRepresentation;

constexpr size_t VMO_SIZE = PAGE_SIZE * 3u;
constexpr size_t PAGE_0 = 0u;
constexpr size_t PAGE_1 = PAGE_SIZE;
constexpr size_t PAGE_2 = PAGE_SIZE * 2u;

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_rights_t GetRights(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : 0u;
}

void FillVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t byte) {
  uint8_t data[length];
  memset(data, byte, length);

  zx_status_t status = vmo.write(data, offset, length);
  ASSERT_EQ(ZX_OK, status);
}

void CheckVmo(const zx::vmo& vmo, size_t offset, size_t length, uint8_t expected_byte) {
  uint8_t data[length];

  zx_status_t status = vmo.read(data, offset, length);
  ASSERT_EQ(ZX_OK, status);

  for (size_t i = 0; i < length; i++) {
    ASSERT_EQ(expected_byte, data[i]);
  }
}

void CheckData(uint8_t* data, size_t offset, size_t length, uint8_t expected_byte) {
  for (size_t i = 0; i < length; i++) {
    ASSERT_EQ(expected_byte, data[i + offset]);
  }
}

void CreateVmoABC(zx::vmo* out_vmo) {
  zx_status_t status = zx::vmo::create(VMO_SIZE, 0u, out_vmo);
  ASSERT_EQ(ZX_OK, status);

  FillVmo(*out_vmo, PAGE_0, PAGE_SIZE, 'A');
  FillVmo(*out_vmo, PAGE_1, PAGE_SIZE, 'B');
  FillVmo(*out_vmo, PAGE_2, PAGE_SIZE, 'C');
}

TEST(VmoFile, Constructor) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // default parameters
  {
    fs::VmoFile file(abc, 0u, PAGE_SIZE);
    EXPECT_EQ(abc.get(), file.vmo_handle());
    EXPECT_EQ(0u, file.offset());
    EXPECT_EQ(PAGE_SIZE, file.length());
    EXPECT_FALSE(file.is_writable());
    EXPECT_EQ(fs::VmoFile::VmoSharing::DUPLICATE, file.vmo_sharing());
  }

  // everything explicit
  {
    fs::VmoFile file(abc, 3u, PAGE_2 + 1u, true, fs::VmoFile::VmoSharing::CLONE_COW);
    EXPECT_EQ(abc.get(), file.vmo_handle());
    EXPECT_EQ(3u, file.offset());
    EXPECT_EQ(PAGE_2 + 1u, file.length());
    EXPECT_TRUE(file.is_writable());
    EXPECT_EQ(fs::VmoFile::VmoSharing::CLONE_COW, file.vmo_sharing());
  }
}

#define EXPECT_RESULT_OK(expr) EXPECT_TRUE((expr).is_ok())
#define EXPECT_RESULT_ERROR(error_val, expr) \
  EXPECT_TRUE((expr).is_error());            \
  EXPECT_EQ(error_val, (expr).error())

TEST(VmoFile, Open) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // read-only
  {
    fs::VmoFile file(abc, 0u, 0u);
    fbl::RefPtr<fs::Vnode> redirect;
    auto result = file.ValidateOptions(VnodeOptions::ReadOnly());
    EXPECT_RESULT_OK(result);
    EXPECT_EQ(ZX_OK, file.Open(result.value(), &redirect));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file.ValidateOptions(VnodeOptions::ReadWrite()));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_ACCESS_DENIED, file.ValidateOptions(VnodeOptions::WriteOnly()));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR, file.ValidateOptions(VnodeOptions().set_directory()));
    EXPECT_NULL(redirect);
  }

  // writable
  {
    fs::VmoFile file(abc, 0u, 0u, true);
    fbl::RefPtr<fs::Vnode> redirect;
    auto result = file.ValidateOptions(VnodeOptions::ReadOnly());
    EXPECT_RESULT_OK(result);
    EXPECT_EQ(ZX_OK, file.Open(result.value(), &redirect));
    EXPECT_NULL(redirect);
    result = file.ValidateOptions(VnodeOptions::ReadWrite());
    EXPECT_RESULT_OK(result);
    EXPECT_EQ(ZX_OK, file.Open(result.value(), &redirect));
    EXPECT_NULL(redirect);
    result = file.ValidateOptions(VnodeOptions::WriteOnly());
    EXPECT_RESULT_OK(result);
    EXPECT_EQ(ZX_OK, file.Open(result.value(), &redirect));
    EXPECT_NULL(redirect);
    EXPECT_RESULT_ERROR(ZX_ERR_NOT_DIR, file.ValidateOptions(VnodeOptions().set_directory()));
    EXPECT_NULL(redirect);
  }
}

TEST(VmoFile, Read) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  uint8_t data[VMO_SIZE];
  memset(data, 0, VMO_SIZE);

  // empty read of non-empty file
  {
    fs::VmoFile file(abc, 0u, PAGE_SIZE);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 0u, 0u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // non-empty read of empty file
  {
    fs::VmoFile file(abc, 0u, 0u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 1u, 0u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // empty read at end of file
  {
    fs::VmoFile file(abc, 0u, 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 0u, 10u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // non-empty read at end of file
  {
    fs::VmoFile file(abc, 0u, 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 1u, 10u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // empty read beyond end of file
  {
    fs::VmoFile file(abc, 0u, 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 0u, 11u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // non-empty read beyond end of file
  {
    fs::VmoFile file(abc, 0u, 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 1u, 11u, &actual));
    EXPECT_EQ(0u, actual);
  }

  // short read of non-empty file
  {
    fs::VmoFile file(abc, PAGE_1 - 3u, 10u);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, 11u, 1u, &actual));
    EXPECT_EQ(9u, actual);
    CheckData(data, 0u, 2u, 'A');
    CheckData(data, 2u, 7u, 'B');
  }

  // full read
  {
    fs::VmoFile file(abc, 0u, VMO_SIZE);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Read(data, VMO_SIZE, 0u, &actual));
    EXPECT_EQ(VMO_SIZE, actual);
    CheckData(data, PAGE_0, PAGE_SIZE, 'A');
    CheckData(data, PAGE_1, PAGE_SIZE, 'B');
    CheckData(data, PAGE_2, PAGE_SIZE, 'C');
  }
}

TEST(VmoFile, Write) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  uint8_t data[VMO_SIZE];
  memset(data, '!', VMO_SIZE);

  // empty write of non-empty file
  {
    fs::VmoFile file(abc, 0u, PAGE_SIZE, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Write(data, 0u, 0u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A');
    CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // non-empty write of empty file
  {
    fs::VmoFile file(abc, 0u, 0u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 0u, &actual));
  }

  // empty write at end of file
  {
    fs::VmoFile file(abc, 0u, 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Write(data, 0u, 10u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A');
    CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // non-empty write at end of file
  {
    fs::VmoFile file(abc, 0u, 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 10u, &actual));
  }

  // empty write beyond end of file
  {
    fs::VmoFile file(abc, 0u, 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Write(data, 0u, 11u, &actual));
    EXPECT_EQ(0u, actual);
    CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A');
    CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // non-empty write beyond end of file
  {
    fs::VmoFile file(abc, 0u, 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_ERR_NO_SPACE, file.Write(data, 1u, 11u, &actual));
  }

  // short write of non-empty file
  {
    fs::VmoFile file(abc, PAGE_1 - 3u, 10u, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Write(data, 11u, 1u, &actual));
    EXPECT_EQ(9u, actual);
    CheckVmo(abc, PAGE_0, PAGE_SIZE - 2u, 'A');
    CheckVmo(abc, PAGE_1 - 2u, 9u, '!');
    CheckVmo(abc, PAGE_1 + 7u, PAGE_SIZE - 7u, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // full write
  {
    fs::VmoFile file(abc, 0u, VMO_SIZE, true);
    size_t actual = UINT64_MAX;
    EXPECT_EQ(ZX_OK, file.Write(data, VMO_SIZE, 0u, &actual));
    EXPECT_EQ(VMO_SIZE, actual);
    CheckVmo(abc, 0u, VMO_SIZE, '!');
  }
}

TEST(VmoFile, Getattr) {
  zx::vmo abc;
  CreateVmoABC(&abc);

  // read-only
  {
    fs::VmoFile file(abc, 0u, PAGE_SIZE * 3u + 117u);
    fs::VnodeAttributes attr;
    EXPECT_EQ(ZX_OK, file.GetAttributes(&attr));
    EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
    EXPECT_EQ(PAGE_SIZE * 3u + 117u, attr.content_size);
    EXPECT_EQ(4u * PAGE_SIZE, attr.storage_size);
    EXPECT_EQ(1u, attr.link_count);
  }

  // writable
  {
    fs::VmoFile file(abc, 0u, PAGE_SIZE * 3u + 117u, true);
    fs::VnodeAttributes attr;
    EXPECT_EQ(ZX_OK, file.GetAttributes(&attr));
    EXPECT_EQ(V_TYPE_FILE | V_IRUSR | V_IWUSR, attr.mode);
    EXPECT_EQ(PAGE_SIZE * 3u + 117u, attr.content_size);
    EXPECT_EQ(4u * PAGE_SIZE, attr.storage_size);
    EXPECT_EQ(1u, attr.link_count);
  }
}

TEST(VmoFile, GetNodeInfo) {
  // sharing = VmoSharing::NONE
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::NONE);
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, file.GetNodeInfo(fs::Rights::ReadOnly(), &info));
  }

  // sharing = VmoSharing::DUPLICATE, read only
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_1 - 5u, 23u, false, fs::VmoFile::VmoSharing::DUPLICATE);
    EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::ReadOnly(), &info));
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ, GetRights(vmo.get()));
    EXPECT_EQ(PAGE_1 - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A');
    CheckVmo(vmo, PAGE_1, 18u, 'B');
  }

  // sharing = VmoSharing::DUPLICATE, read-write
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
    EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::ReadWrite(), &info));
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE,
              GetRights(vmo.get()));
    EXPECT_EQ(PAGE_1 - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    CheckVmo(vmo, PAGE_1 - 5u, 5u, 'A');
    CheckVmo(vmo, PAGE_1, 18u, 'B');

    FillVmo(vmo, PAGE_1 - 5u, 23u, '!');

    CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A');
    CheckVmo(abc, PAGE_1 - 5u, 23u, '!');
    CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // sharing = VmoSharing::DUPLICATE, write only
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_1 - 5u, 23u, true, fs::VmoFile::VmoSharing::DUPLICATE);
    EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::WriteOnly(), &info));
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_EQ(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_WRITE, GetRights(vmo.get()));
    EXPECT_EQ(PAGE_1 - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    FillVmo(vmo, PAGE_1 - 5u, 23u, '!');

    CheckVmo(abc, 0u, PAGE_SIZE - 5u, 'A');
    CheckVmo(abc, PAGE_1 - 5u, 23u, '!');
    CheckVmo(abc, PAGE_1 + 18u, PAGE_SIZE - 18u, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // sharing = VmoSharing::CLONE_COW, read only
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_2 - 5u, 23u, false, fs::VmoFile::VmoSharing::CLONE_COW);
    // There is non-trivial lazy initialization happening here - repeat it
    // to make sure it's nice and deterministic.
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::ReadOnly(), &info));
    }
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ, GetRights(vmo.get()));
    EXPECT_EQ(PAGE_SIZE - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B');
    CheckVmo(vmo, PAGE_SIZE, 18u, 'C');
  }

  // sharing = VmoSharing::CLONE_COW, read-write
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
    EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::ReadWrite(), &info));
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE,
              GetRights(vmo.get()));
    EXPECT_EQ(PAGE_SIZE - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    CheckVmo(vmo, PAGE_SIZE - 5u, 5u, 'B');
    CheckVmo(vmo, PAGE_SIZE, 18u, 'C');

    FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!');

    CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A');
    CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }

  // sharing = VmoSharing::CLONE_COW, write only
  {
    zx::vmo abc;
    CreateVmoABC(&abc);

    fs::VnodeRepresentation info;
    fs::VmoFile file(abc, PAGE_2 - 5u, 23u, true, fs::VmoFile::VmoSharing::CLONE_COW);
    EXPECT_EQ(ZX_OK, file.GetNodeInfo(fs::Rights::WriteOnly(), &info));
    ASSERT_TRUE(info.is_memory());
    VnodeInfo::Memory& memory = info.memory();
    zx::vmo vmo = std::move(memory.vmo);
    EXPECT_NE(abc.get(), vmo.get());
    EXPECT_NE(GetKoid(abc.get()), GetKoid(vmo.get()));
    EXPECT_EQ(ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_WRITE, GetRights(vmo.get()));
    EXPECT_EQ(PAGE_SIZE - 5u, memory.offset);
    EXPECT_EQ(23u, memory.length);

    FillVmo(vmo, PAGE_SIZE - 5u, 23u, '!');

    CheckVmo(abc, PAGE_0, PAGE_SIZE, 'A');
    CheckVmo(abc, PAGE_1, PAGE_SIZE, 'B');
    CheckVmo(abc, PAGE_2, PAGE_SIZE, 'C');
  }
}

}  // namespace
