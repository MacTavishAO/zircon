// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXECUTOR_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXECUTOR_H_

#include <fbl/ref_ptr.h>
#include <ktl/unique_ptr.h>
#include <object/job_dispatcher.h>
#include <object/memory_watchdog.h>
#include <object/root_job_observer.h>

// An Executor encapsulates the kernel state necessary to implement the Zircon system calls. It
// depends on an interface from the kernel below it, presenting primitives like threads and wait
// queues. It presents an interface to the system call implementations.
//
// The goals of factoring this into such a layer include:
//
// - The ability to test code in this layer separately from low-level kernel implementation details,
//   and from the syscall mechanism. This includes correctness as well as performance tests.
//
// - Centralize resource management in order to make progress on things like not reporting
//   ZX_ERR_NO_MEMORY when creating a zx::event, or reporting bad handle faults.
//
// TODO(kulakowski) The above comment is aspirational. So far, only the root job (and its observer)
// is managed by the Executor. Other subsystems, like port arenas and handle arenas, are not yet
// included. And e.g. tests are not yet written against the Executor.
class Executor {
 public:
  void Init();

  const fbl::RefPtr<JobDispatcher>& GetRootJobDispatcher() { return root_job_; }

  fbl::RefPtr<EventDispatcher> GetMemPressureEvent(uint32_t kind) {
    return memory_watchdog_.GetMemPressureEvent(kind);
  }

  // Start watching the root job, taking a system-level action (such as restart) if
  // all its children are removed.
  //
  // This must be called after the root job has at least one child process or child job.
  void StartRootJobObserver();

 private:
  // All jobs and processes of this Executor are rooted at this job.
  fbl::RefPtr<JobDispatcher> root_job_;

  // Watch the root job, taking action (such as a system reboot) if it ends up
  // with no children.
  ktl::unique_ptr<RootJobObserver> root_job_observer_;

  // The memory watchdog for this Executor. When it observes low memory
  // conditions, it notifies the root job of this executor.
  MemoryWatchdog memory_watchdog_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_EXECUTOR_H_
