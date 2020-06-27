// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <src/lib/chunked-compression/compression-params.h>
#include <zstd/zstd.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

// Simple basic conversion test.
TEST(CompressionSettingsTest, CompressionAlgorithmToStringConvertLZ4) {
  ASSERT_STR_EQ(CompressionAlgorithmToString(CompressionAlgorithm::LZ4), "LZ4");
}

// Create an invalid enum to trigger assert.
TEST(CompressionSettingsTest, CompressionAlgorithmToStringConvertUndefinedEnum) {
  ASSERT_DEATH(([] { CompressionAlgorithmToString(static_cast<CompressionAlgorithm>(9999)); }),
               "Enum value 9999 did not fail conversion.");
}

// Simple basic conversion for compression enabled.
TEST(CompressionSettingsTest, AlgorithmForInodeConvertLZ4) {
  Inode inode;
  inode.header.flags = kBlobFlagLZ4Compressed;
  ASSERT_EQ(AlgorithmForInode(inode), CompressionAlgorithm::LZ4);
}

// Conversion when no compression flags are enabled.
TEST(CompressionSettingsTest, AlgorithmForInodeConvertUncompressed) {
  Inode inode;
  inode.header.flags &= ~kBlobFlagMaskAnyCompression;
  ASSERT_EQ(AlgorithmForInode(inode), CompressionAlgorithm::UNCOMPRESSED);
}

// Simple basic conversion test.
TEST(CompressionSettingsTest, CompressionInodeHeaderFlagsConvertLZ4) {
  ASSERT_EQ(CompressionInodeHeaderFlags(CompressionAlgorithm::LZ4), kBlobFlagLZ4Compressed);
}

// Create an invalid enum to trigger assert.
TEST(CompressionSettingsTest, CompressionInodeHeaderFlagsConvertUndefinedEnum) {
  ASSERT_DEATH(([] { CompressionInodeHeaderFlags(static_cast<CompressionAlgorithm>(9999)); }),
               "Enum value 9999 did not fail conversion.");
}

// Apply a couple of CompressionAlgorithms, verify that they come back right
// despite multiple calls.
TEST(CompressionSettingsTest, SetCompressionAlgorithmCalledTwice) {
  Inode inode;
  inode.header.flags = kBlobFlagAllocated;  // Ensure that this stays set.
  SetCompressionAlgorithm(&inode, CompressionAlgorithm::LZ4);
  ASSERT_EQ(inode.header.flags, kBlobFlagLZ4Compressed | kBlobFlagAllocated);
  SetCompressionAlgorithm(&inode, CompressionAlgorithm::ZSTD);
  ASSERT_EQ(inode.header.flags, kBlobFlagZSTDCompressed | kBlobFlagAllocated);
}

// Anything is valid with no compression level setings.
TEST(CompressionSettingsTest, IsValidWithNoSettings) {
  CompressionSettings settings = {CompressionAlgorithm::UNCOMPRESSED, std::nullopt};
  ASSERT_TRUE(settings.IsValid());
}

// There should be no compression settings for UNCOMPRESSED.
TEST(CompressionSettingsTest, IsValidCompressionLevelUncompressed) {
  CompressionSettings settings = {CompressionAlgorithm::UNCOMPRESSED, 4};
  ASSERT_FALSE(settings.IsValid());
}

// Check range limits on ZSTD.
TEST(CompressionSettingsTest, IsValidCompressionLevelZSTD) {
  CompressionSettings settings = {CompressionAlgorithm::ZSTD, ZSTD_minCLevel()};
  ASSERT_TRUE(settings.IsValid());
  settings.compression_level = ZSTD_maxCLevel() + 1;
  ASSERT_FALSE(settings.IsValid());
}

// Check range limits on Chunked compression.
TEST(CompressionSettingsTest, IsValidCompressionLevelChunked) {
  CompressionSettings settings = {CompressionAlgorithm::CHUNKED,
                                  chunked_compression::CompressionParams::MinCompressionLevel()};
  ASSERT_TRUE(settings.IsValid());
  settings.compression_level = chunked_compression::CompressionParams::MaxCompressionLevel() + 1;
  ASSERT_FALSE(settings.IsValid());
}

}  // namespace
}  // namespace blobfs
