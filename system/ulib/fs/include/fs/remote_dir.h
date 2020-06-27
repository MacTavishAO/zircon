// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_REMOTE_DIR_H_
#define FS_REMOTE_DIR_H_

#include <fbl/macros.h>

#include "vnode.h"

namespace fs {

// A remote directory holds a channel to a remotely hosted directory to
// which requests are delegated when opened.
//
// This class is designed to allow programs to publish remote filesystems
// as directories without requiring a separate "mount" step.  In effect,
// a remote directory is "mounted" at creation time.
//
// It is not possible for the client to detach the remote directory or
// to mount a new one in its place.
//
// This class is thread-safe.
class RemoteDir : public Vnode {
 public:
  // Binds to a remotely hosted directory using the specified FIDL client
  // channel endpoint.  The channel must be valid.
  explicit RemoteDir(zx::channel remote_dir_client);

  // Releases the remotely hosted directory.
  ~RemoteDir() override;

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(VnodeAttributes* a) final;
  bool IsRemote() const final;
  zx_handle_t GetRemote() const final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;

 private:
  zx::channel const remote_dir_client_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RemoteDir);
};

}  // namespace fs

#endif  // FS_REMOTE_DIR_H_
