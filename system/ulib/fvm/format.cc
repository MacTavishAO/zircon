// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/volume/c/fidl.h>
#endif

#include <zircon/assert.h>

#include <fvm/format.h>

namespace fvm {
namespace {

// Used to check whether a given VPartitionEntry is flagged as an inactive partition.
// This flags are a mirror of those exposed in the fidl interface. Since this code is used in host
// too, we can't rely on them directly, but enforce compile time checks that the values match.
constexpr uint32_t kVPartitionEntryFlagMask = 0x00000001;
constexpr uint32_t kVPartitionEntryFlagInactive = 0x00000001;

#ifdef __Fuchsia__
// Enforce target and host flags to match.
static_assert(kVPartitionEntryFlagInactive ==
                  fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE,
              "Inactive Flag must match FIDL definition.");
#endif

// Slice Entry mask for retrieving the assigned partition.
constexpr uint64_t kVPartitionEntryMax = (1ull << kSliceEntryVPartitionBits) - 1;
constexpr uint64_t kVPartitionEntryMask = kVPartitionEntryMax;

static_assert(kMaxVPartitions <= kVPartitionEntryMax,
              "VPartition addres space needs to fit within Slice Entry VPartitionBits.");

// Slice Entry mask for retrieving the assigned vslice.
constexpr uint64_t kSliceEntryVSliceMax = (1ull << kSliceEntryVSliceBits) - 1;
constexpr uint64_t kSliceEntryVSliceMask = kSliceEntryVSliceMax << kSliceEntryVPartitionBits;

static_assert(kSliceEntryVSliceMax >= fvm::kMaxVSlices,
              "SliceEntry must be able to address the range [0. kMaxVSlice)");

// Remaining bits.
constexpr uint64_t kSliceEntryReservedBits = 16;

static_assert(kSliceEntryVPartitionBits + kSliceEntryVSliceBits + kSliceEntryReservedBits == 64,
              "Exceeding SliceEntry payload size.");

}  // namespace

VPartitionEntry VPartitionEntry::Create(const uint8_t* type, const uint8_t* guid, uint32_t slices,
                                        const char* name, uint32_t flags) {
  VPartitionEntry entry = VPartitionEntry::Create();
  entry.slices = slices;
  // Filter out unallowed flags.
  entry.flags = ParseFlags(flags);
  memcpy(&entry.type, type, kGuidSize);
  memcpy(&entry.guid, guid, kGuidSize);
  memcpy(&entry.name, name, kMaxVPartitionNameLength);
  return entry;
}

uint32_t VPartitionEntry::ParseFlags(uint32_t raw_flags) {
  return raw_flags & kVPartitionEntryFlagMask;
}

bool VPartitionEntry::IsActive() const { return (flags & kVPartitionEntryFlagInactive) == 0; }

bool VPartitionEntry::IsInactive() const { return !IsActive(); }

bool VPartitionEntry::IsAllocated() const { return slices != 0; }

bool VPartitionEntry::IsFree() const { return !IsAllocated(); }

void VPartitionEntry::Release() {
  memset(this, 0, sizeof(VPartitionEntry));
  ZX_ASSERT_MSG(IsFree(), "VPartitionEntry must be free after calling VPartitionEntry::Release()");
}

void VPartitionEntry::SetActive(bool is_active) {
  if (is_active) {
    flags &= (~kVPartitionEntryFlagInactive);
  } else {
    flags |= kVPartitionEntryFlagInactive;
  }
}

SliceEntry SliceEntry::Create(uint64_t vpartition, uint64_t vslice) {
  SliceEntry entry;
  entry.Set(vpartition, vslice);
  return entry;
}

void SliceEntry::Set(uint64_t vpartition, uint64_t vslice) {
  ZX_ASSERT(vpartition < kVPartitionEntryMax);
  ZX_ASSERT(vslice < kSliceEntryVSliceMax);
  data = 0ull | (vpartition & kVPartitionEntryMax) |
         ((vslice & kSliceEntryVSliceMax) << (kSliceEntryVPartitionBits));
}

void SliceEntry::Release() { data = 0; }

bool SliceEntry::IsAllocated() const { return VPartition() != 0; }

bool SliceEntry::IsFree() const { return !IsAllocated(); }

uint64_t SliceEntry::VSlice() const {
  uint64_t vslice = (data & kSliceEntryVSliceMask) >> kSliceEntryVPartitionBits;
  ZX_ASSERT_MSG(vslice < (1ul << kSliceEntryVSliceBits), "Slice assigned to vslice out of range.");
  return vslice;
}

uint64_t SliceEntry::VPartition() const {
  uint64_t vpartition = (data & kVPartitionEntryMask);
  ZX_ASSERT_MSG(vpartition < kMaxVPartitions, "Slice assigned to Partition out of range.");
  return vpartition;
}

}  // namespace fvm
