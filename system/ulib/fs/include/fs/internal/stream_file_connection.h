// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_STREAM_FILE_CONNECTION_H_
#define FS_INTERNAL_STREAM_FILE_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/llcpp/fidl.h>

#include <fs/internal/file_connection.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

class StreamFileConnection final : public FileConnection {
 public:
  // Refer to documentation for |Connection::Connection|.
  StreamFileConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::stream stream,
                       VnodeProtocol protocol, VnodeConnectionOptions options);

  ~StreamFileConnection() final = default;

 private:
  //
  // |fuchsia.io/File| operations.
  //

  void Read(uint64_t count, ReadCompleter::Sync completer) final;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) final;
  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) final;
  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) final;
  void Seek(int64_t offset, llcpp::fuchsia::io::SeekOrigin start,
            SeekCompleter::Sync completer) final;

  zx::stream stream_;
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_STREAM_FILE_CONNECTION_H_
