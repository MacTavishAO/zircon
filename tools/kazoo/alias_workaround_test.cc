// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/test.h"
#include "tools/kazoo/test_ir_test_aliases.test.h"

namespace {

TEST(AliasWorkaround, Mappings) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_aliases, &library));

  EXPECT_EQ(library.name(), "zx");
  ASSERT_EQ(library.syscalls().size(), 1u);

  const auto& sc = library.syscalls()[0];
  EXPECT_EQ(sc->name(), "aliases_some_func");
  EXPECT_EQ(GetCUserModeName(sc->kernel_return_type()), "zx_status_t");

  // See test_aliases.test.fidl for this giant function's fidl spec. This covers all the aliases
  // required to map all syscalls today. We should be able to whittle these down over time and
  // eventually delete this mapping and test entirely.
  size_t cur_arg = 0;

#define CHECK_ARG(_type, _name)                                               \
  EXPECT_EQ(sc->kernel_arguments()[cur_arg].name(), _name);                   \
  EXPECT_EQ(GetCUserModeName(sc->kernel_arguments()[cur_arg].type()), _type); \
  ++cur_arg;

  // charptr
  CHECK_ARG("char*", "a");

  // const_futexptr
  CHECK_ARG("const zx_futex_t*", "b");

  // const_voidptr
  CHECK_ARG("const void*", "c");

  // mutable_string
  CHECK_ARG("char*", "d");
  CHECK_ARG("size_t", "d_size");

  // mutable_uint32
  CHECK_ARG("uint32_t*", "e");

  // mutable_usize
  CHECK_ARG("size_t*", "f");

  // mutable_vector_HandleDisposition_u32size
  CHECK_ARG("zx_handle_disposition_t*", "g");
  CHECK_ARG("uint32_t", "num_g");

  // mutable_vector_WaitItem
  CHECK_ARG("zx_wait_item_t*", "h");
  CHECK_ARG("size_t", "num_h");

  // mutable_vector_handle_u32size
  CHECK_ARG("zx_handle_t*", "i");
  CHECK_ARG("uint32_t", "num_i");

  // mutable_vector_void
  CHECK_ARG("void*", "j");
  CHECK_ARG("size_t", "j_size");

  // mutable_vector_void_u32size
  CHECK_ARG("void*", "k");
  CHECK_ARG("uint32_t", "k_size");

  // vector_HandleInfo_u32size
  CHECK_ARG("const zx_handle_info_t*", "l");
  CHECK_ARG("uint32_t", "num_l");

  // vector_handle_u32size
  CHECK_ARG("const zx_handle_t*", "m");
  CHECK_ARG("uint32_t", "num_m");

  // vector_paddr
  CHECK_ARG("const zx_paddr_t*", "n");
  CHECK_ARG("size_t", "num_n");

  // vector_void
  CHECK_ARG("const void*", "o");
  CHECK_ARG("size_t", "o_size");

  // vector_void_u32size
  CHECK_ARG("const void*", "p");
  CHECK_ARG("uint32_t", "p_size");

  // voidptr
  CHECK_ARG("void*", "q");

  // Optionality only shows up in __NONNULL() header markup, not the actual type info when it's
  // converted to a C type, so check that setting specifically for the optional outputs.
#define CHECK_IS_OPTIONAL() \
  EXPECT_TRUE(sc->kernel_arguments()[cur_arg].type().optionality() == Optionality::kOutputOptional);

  CHECK_IS_OPTIONAL();
  CHECK_ARG("zx_pci_bar_t*", "r");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("zx_port_packet_t*", "s");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("zx_koid_t*", "t");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("zx_signals_t*", "u");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("zx_time_t*", "v");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("uint32_t*", "w");

  CHECK_IS_OPTIONAL();
  CHECK_ARG("size_t*", "x");

  CHECK_ARG("zx_string_view_t*", "y");

#undef CHECK_IS_OPTIONAL
#undef CHECK_ARG

  EXPECT_EQ(cur_arg, 36u);  // 25 fidl args + 11 that expand to pointer+size.
}

}  // namespace
