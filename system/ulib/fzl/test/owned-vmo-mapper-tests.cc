// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>

#include <iterator>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <unittest/unittest.h>

// Note: these tests focus on the added functionality of the owned VMO
// mapper.  The core functionality is assumed to have already been tested by the
// vmo/vmar tests.
namespace {

constexpr char vmo_name[ZX_MAX_NAME_LEN] = "my-vmo";
constexpr size_t kNonRootVmarSize = (512 << 20);
constexpr zx_vm_option_t kNonRootVmarOpts =
    ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;

bool ValidateCreateHelper(const fzl::OwnedVmoMapper& mapper, uint64_t size) {
  BEGIN_HELPER;

  ASSERT_TRUE(mapper.vmo().is_valid());
  ASSERT_EQ(mapper.size(), size);
  ASSERT_NONNULL(mapper.start());

  auto data = static_cast<const uint8_t*>(mapper.start());
  for (size_t i = 0; i < size; ++i) {
    ASSERT_EQ(data[i], 0);
  }

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper.vmo().get_property(ZX_PROP_NAME, name, std::size(name));
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < std::size(name); ++i) {
    ASSERT_EQ(name[i], vmo_name[i]);
  }

  END_HELPER;
}

template <bool NON_ROOT_VMAR>
bool UncheckedCreateHelper(std::unique_ptr<fzl::OwnedVmoMapper>* out_mapper, uint64_t size,
                           const char* name,
                           zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                           uint32_t cache_policy = 0) {
  BEGIN_HELPER;

  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NONNULL(manager);
  }

  ASSERT_NONNULL(out_mapper);
  auto mapper = std::make_unique<fzl::OwnedVmoMapper>();
  if (mapper->CreateAndMap(size, name, map_options, std::move(manager), cache_policy) == ZX_OK) {
    *out_mapper = std::move(mapper);
  }
  END_HELPER;
}

template <bool NON_ROOT_VMAR>
bool CreateHelper(std::unique_ptr<fzl::OwnedVmoMapper>* out_mapper, uint64_t size, const char* name,
                  zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                  uint32_t cache_policy = 0) {
  BEGIN_HELPER;

  ASSERT_TRUE(
      UncheckedCreateHelper<NON_ROOT_VMAR>(out_mapper, size, name, map_options, cache_policy));
  ASSERT_NONNULL(*out_mapper);
  ASSERT_TRUE(ValidateCreateHelper(**out_mapper, size));

  END_HELPER;
}

template <bool NON_ROOT_VMAR>
bool CreateAndMapHelper(fzl::OwnedVmoMapper* inout_mapper, uint64_t size, const char* name,
                        zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                        uint32_t cache_policy = 0) {
  BEGIN_HELPER;

  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NONNULL(manager);
  }

  ASSERT_NONNULL(inout_mapper);
  zx_status_t status;
  status = inout_mapper->CreateAndMap(size, name, map_options, std::move(manager), cache_policy);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE(ValidateCreateHelper(*inout_mapper, size));

  END_HELPER;
}

template <bool NON_ROOT_VMAR>
bool MapHelper(fzl::OwnedVmoMapper* inout_mapper, zx::vmo vmo, uint64_t size,
               zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE) {
  BEGIN_HELPER;

  fbl::RefPtr<fzl::VmarManager> manager;
  if (NON_ROOT_VMAR) {
    manager = fzl::VmarManager::Create(kNonRootVmarSize, nullptr, kNonRootVmarOpts);
    ASSERT_NONNULL(manager);
  }

  ASSERT_NONNULL(inout_mapper);
  zx_status_t status;
  status = inout_mapper->Map(std::move(vmo), size, map_options, std::move(manager));
  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE(ValidateCreateHelper(*inout_mapper, size));

  END_HELPER;
}

template <bool NON_ROOT_VMAR>
bool CreateTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool CreateAndMapTest() {
  BEGIN_TEST;

  fzl::OwnedVmoMapper mapper;
  ASSERT_TRUE(CreateAndMapHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool MapTest() {
  BEGIN_TEST;

  zx::vmo vmo;
  zx_status_t status;

  status = zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo);
  ASSERT_EQ(status, ZX_OK);

  status = vmo.set_property(ZX_PROP_NAME, vmo_name, strlen(vmo_name));
  ASSERT_EQ(status, ZX_OK);

  fzl::OwnedVmoMapper mapper;
  ASSERT_TRUE(MapHelper<NON_ROOT_VMAR>(&mapper, std::move(vmo), ZX_PAGE_SIZE));

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool MoveTest() {
  BEGIN_TEST;

  fzl::OwnedVmoMapper mapper1;
  ASSERT_TRUE(CreateAndMapHelper<NON_ROOT_VMAR>(&mapper1, ZX_PAGE_SIZE, vmo_name));

  // Move by construction
  zx_handle_t orig_handle = mapper1.vmo().get();
  void* orig_start = mapper1.start();
  size_t orig_size = mapper1.size();
  const fzl::VmarManager* orig_manager = mapper1.manager().get();

  ASSERT_NE(orig_handle, ZX_HANDLE_INVALID);
  ASSERT_NONNULL(orig_start);
  ASSERT_EQ(orig_size, ZX_PAGE_SIZE);
  if (NON_ROOT_VMAR) {
    ASSERT_NONNULL(orig_manager);
  } else {
    ASSERT_NULL(orig_manager);
  }

  fzl::OwnedVmoMapper mapper2(std::move(mapper1));
  ASSERT_EQ(mapper1.vmo().get(), ZX_HANDLE_INVALID);
  ASSERT_NULL(mapper1.start());
  ASSERT_EQ(mapper1.size(), 0);
  ASSERT_NULL(mapper1.manager());

  ASSERT_EQ(mapper2.vmo().get(), orig_handle);
  ASSERT_EQ(mapper2.start(), orig_start);
  ASSERT_EQ(mapper2.size(), orig_size);
  ASSERT_EQ(mapper2.manager().get(), orig_manager);
  ASSERT_TRUE(ValidateCreateHelper(mapper2, orig_size));

  // Move by assignment
  mapper1 = std::move(mapper2);

  ASSERT_EQ(mapper2.vmo().get(), ZX_HANDLE_INVALID);
  ASSERT_NULL(mapper2.start());
  ASSERT_EQ(mapper2.size(), 0);
  ASSERT_NULL(mapper2.manager());

  ASSERT_EQ(mapper1.vmo().get(), orig_handle);
  ASSERT_EQ(mapper1.start(), orig_start);
  ASSERT_EQ(mapper1.size(), orig_size);
  ASSERT_EQ(mapper1.manager().get(), orig_manager);
  ASSERT_TRUE(ValidateCreateHelper(mapper1, orig_size));

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool ReadTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  uint8_t bytes[ZX_PAGE_SIZE];
  memset(bytes, 0xff, ZX_PAGE_SIZE);

  zx_status_t status = mapper->vmo().read(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(bytes[i], 0);
  }

  END_TEST;
}

// Test that touching memory, then zx_vmo_reading, works as expected.
template <bool NON_ROOT_VMAR>
bool WriteMappingTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  auto data = static_cast<uint8_t*>(mapper->start());
  memset(data, 0xff, ZX_PAGE_SIZE);

  uint8_t bytes[ZX_PAGE_SIZE] = {};
  zx_status_t status = mapper->vmo().read(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(bytes[i], 0xff);
  }

  END_TEST;
}

// Test that zx_vmo_writing, then reading memory, works as expected.
template <bool NON_ROOT_VMAR>
bool ReadMappingTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(CreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, vmo_name));

  uint8_t bytes[ZX_PAGE_SIZE];
  memset(bytes, 0xff, ZX_PAGE_SIZE);
  zx_status_t status = mapper->vmo().write(bytes, 0, ZX_PAGE_SIZE);
  ASSERT_EQ(status, ZX_OK);

  auto data = static_cast<uint8_t*>(mapper->start());
  for (size_t i = 0; i < ZX_PAGE_SIZE; ++i) {
    ASSERT_EQ(data[i], 0xff);
  }

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool EmptyNameTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, ""));
  ASSERT_NONNULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
    ASSERT_EQ(name[i], 0);
  }

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool NullptrNameTest() {
  BEGIN_TEST;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, nullptr));
  ASSERT_NONNULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN; ++i) {
    ASSERT_EQ(name[i], 0);
  }

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool LongNameTest() {
  BEGIN_TEST;

  char long_name[ZX_PAGE_SIZE];
  memset(long_name, 'x', ZX_PAGE_SIZE);
  long_name[ZX_PAGE_SIZE - 1] = 0;

  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, ZX_PAGE_SIZE, long_name));
  ASSERT_NONNULL(mapper);

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = mapper->vmo().get_property(ZX_PROP_NAME, name, ZX_MAX_NAME_LEN);
  ASSERT_EQ(status, ZX_OK);
  for (size_t i = 0; i < ZX_MAX_NAME_LEN - 1; ++i) {
    ASSERT_EQ(name[i], 'x');
  }
  ASSERT_EQ(name[ZX_MAX_NAME_LEN - 1], 0);

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool GoodSizesTest() {
  BEGIN_TEST;

  static const size_t sizes[] = {
      ZX_PAGE_SIZE,
      16 * ZX_PAGE_SIZE,
      ZX_PAGE_SIZE * ZX_PAGE_SIZE,
      ZX_PAGE_SIZE + 1,
  };

  for (size_t size : sizes) {
    std::unique_ptr<fzl::OwnedVmoMapper> mapper;
    ASSERT_TRUE(CreateHelper<NON_ROOT_VMAR>(&mapper, size, vmo_name));
  }

  END_TEST;
}

template <bool NON_ROOT_VMAR>
bool BadSizesTest() {
  BEGIN_TEST;

  // Size 0 should fail.
  std::unique_ptr<fzl::OwnedVmoMapper> mapper;
  ASSERT_TRUE(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, 0, vmo_name));
  ASSERT_NULL(mapper);

  // So should an aburdly big request.
  ASSERT_TRUE(UncheckedCreateHelper<NON_ROOT_VMAR>(&mapper, SIZE_MAX, vmo_name));
  ASSERT_NULL(mapper);

  END_TEST;
}

}  // namespace

#define MAKE_TEST(_name)                           \
  RUN_NAMED_TEST(#_name "_RootVmar", _name<false>) \
  RUN_NAMED_TEST(#_name "_NON_ROOT_VMAR", _name<true>)

BEGIN_TEST_CASE(owned_vmo_mapper_tests)
MAKE_TEST(CreateTest)
MAKE_TEST(CreateAndMapTest)
MAKE_TEST(MapTest)
MAKE_TEST(MoveTest)
MAKE_TEST(ReadTest)
MAKE_TEST(WriteMappingTest)
MAKE_TEST(ReadMappingTest)
MAKE_TEST(EmptyNameTest)
MAKE_TEST(NullptrNameTest)
MAKE_TEST(LongNameTest)
MAKE_TEST(GoodSizesTest)
MAKE_TEST(BadSizesTest)
END_TEST_CASE(owned_vmo_mapper_tests)

#undef MAKE_TEST
