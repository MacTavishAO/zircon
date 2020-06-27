// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Tries to get a handle to the task with the specified koid.
// Return values:
//   ZX_OK: The task was found: |*out| is a handle to it, and |*type| indicates
//       whether it's a job, process, or thread
//       (ZX_OBJ_TYPE_JOB/PROCESS/THREAD). The caller is responsible for closing
//       the handle.
//   ZX_ERR_NOT_FOUND: Could not find a task with the specified koid.
//   ZX_ERR_INVALID_ARGS: |type| or |out| is NULL.
// Will fail if the calling process does not have the rights to access the root
// job.
zx_status_t get_task_by_koid(zx_koid_t koid, zx_obj_type_t* type, zx_handle_t* out);
// TODO(dbort): Add a "desired type" so we don't walk every thread in the
// system just to find a job.

__END_CDECLS
