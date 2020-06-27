// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <inttypes.h>
#include <lib/console.h>
#include <lib/debuglog.h>
#include <string.h>

#include <new>

#include <arch/ops.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/atomic.h>
#include <lk/init.h>
#include <lockdep/lockdep.h>
#include <vm/vm.h>

#if WITH_LOCK_DEP

namespace {

// Event to wake up the loop detector thread when a new edge is added to the
// lock dependency graph.
AutounsignalEvent graph_edge_event;

// Event to wait on the completion of a triggered loop detection pass. This is
// primarily to bound the async loop detection report when testing.
Event detection_complete_event;

// Synchronizes the access to the loop detection completion event.
DECLARE_SINGLETON_MUTEX(DetectionCompleteLock);

// Loop detection thread. Traverses the lock dependency graph to find circular
// lock dependencies.
int LockDepThread(void* /*arg*/) {
  while (true) {
    __UNUSED zx_status_t error = graph_edge_event.Wait();

    // Add some hysteresis to avoid re-triggering the loop detector on
    // close successive updates to the graph and to give the inline
    // validation reports a chance to print out first.
    Thread::Current::SleepRelative(ZX_SEC(2));

    lockdep::LoopDetectionPass();
    detection_complete_event.Signal();
  }
  return 0;
}

void LockDepInit(unsigned /*level*/) {
  Thread* t = Thread::Create("lockdep", &LockDepThread, NULL, LOW_PRIORITY);
  t->DetachAndResume();
}

// Dumps the state of the lock dependency graph.
void DumpLockClassState() {
  printf("Lock class states:\n");
  for (auto& state : lockdep::LockClassState::Iter()) {
    printf("  %s {\n", state.name());
    for (lockdep::LockClassId id : state.dependency_set()) {
      printf("    %s\n", lockdep::LockClassState::GetName(id));
    }
    printf("  }\n");
  }
  printf("\nConnected sets:\n");
  for (auto& state : lockdep::LockClassState::Iter()) {
    // Only handle root nodes in the outer loop. The nested loop will pick
    // up all of the child nodes under each parent node.
    if (state.connected_set() == &state) {
      printf("{\n");
      for (auto& other_state : lockdep::LockClassState::Iter()) {
        if (other_state.connected_set() == &state)
          printf("  %s\n", other_state.name());
      }
      printf("}\n");
    }
  }
}

// Top-level lockdep command.
int CommandLockDep(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("Not enough arguments:\n");
  usage:
    printf("%s dump              : dump lock classes\n", argv[0].str);
    printf("%s loop              : trigger loop detection pass\n", argv[0].str);
    return -1;
  }

  if (strcmp(argv[1].str, "dump") == 0) {
    DumpLockClassState();
  } else if (strcmp(argv[1].str, "loop") == 0) {
    printf("Triggering loop detection pass:\n");
    lockdep::SystemTriggerLoopDetection();
  } else {
    printf("Unrecognized subcommand: '%s'\n", argv[1].str);
    goto usage;
  }

  return 0;
}

}  // anonymous namespace

// Triggers a loop detection pass and waits for it to complete or timeout.
zx_status_t TriggerAndWaitForLoopDetection(zx_time_t deadline) {
  Guard<Mutex> guard{DetectionCompleteLock::Get()};
  detection_complete_event.Unsignal();
  lockdep::SystemTriggerLoopDetection();
  return detection_complete_event.WaitDeadline(deadline, Interruptible::Yes);
}

STATIC_COMMAND_START
STATIC_COMMAND("lockdep", "kernel lock diagnostics", &CommandLockDep)
STATIC_COMMAND_END(lockdep)

LK_INIT_HOOK(lockdep, LockDepInit, LK_INIT_LEVEL_THREADING)

namespace lockdep {

// Prints a kernel oops when a normal lock order violation is detected.
void SystemLockValidationError(AcquiredLockEntry* bad_entry, AcquiredLockEntry* conflicting_entry,
                               ThreadLockState* state, void* caller_address, void* caller_frame,
                               LockResult result) {
  Thread* const current_thread = Thread::Current::Get();

  char owner_name[ZX_MAX_NAME_LEN];
  current_thread->OwnerName(owner_name);

  const uint64_t user_pid = current_thread->user_pid_;
  const uint64_t user_tid = current_thread->user_tid_;

  DLOG_KERNEL_OOPS("Lock validation failed for thread %p pid %" PRIu64 " tid %" PRIu64
                   " (%s:%s):\n",
                   current_thread, user_pid, user_tid, owner_name, current_thread->name());
  printf("Reason: %s\n", ToString(result));
  printf("Bad lock: name=%s order=%" PRIu64 "\n", LockClassState::GetName(bad_entry->id()),
         bad_entry->order());
  printf("Conflict: name=%s order=%" PRIu64 "\n", LockClassState::GetName(conflicting_entry->id()),
         conflicting_entry->order());
  printf("caller=%p frame=%p\n", caller_address, caller_frame);

  Thread::Current::PrintBacktraceAtFrame(caller_frame);
  printf("\n");
}

// Issues a kernel panic when a fatal lock order violation is detected.
void SystemLockValidationFatal(AcquiredLockEntry* lock_entry, ThreadLockState* state,
                               void* caller_address, void* caller_frame, LockResult result) {
  panic("Fatal lock violation detected! name=%s, reason=%s, pc=%p, stack frame=%p\n",
        LockClassState::GetName(lock_entry->id()), ToString(result), caller_address, caller_frame);
}

// Prints a kernel oops when a circular lock dependency is detected.
void SystemCircularLockDependencyDetected(LockClassState* connected_set_root) {
  DLOG_KERNEL_OOPS("Circular lock dependency detected:\n");

  for (auto& node : lockdep::LockClassState::Iter()) {
    if (node.connected_set() == connected_set_root)
      printf("  %s\n", node.name());
  }

  printf("\n");
}

// Returns a pointer to the ThreadLockState instance for the current thread when
// thread context or for the current CPU when in irq context.
ThreadLockState* SystemGetThreadLockState() {
  if (arch_blocking_disallowed()) {
    return &get_local_percpu()->lock_state;
  }
  return &Thread::Current::Get()->lock_state_;
}

// Initializes an instance of ThreadLockState.
void SystemInitThreadLockState(ThreadLockState*) {}

// Wakes up the loop detector thread to re-evaluate the dependency graph.
void SystemTriggerLoopDetection() {
  if (ThreadLock::Get()->lock().IsHeld()) {
    AssertHeld<ThreadLock, IrqSave>(*ThreadLock::Get());
    graph_edge_event.SignalThreadLocked();
  } else {
    graph_edge_event.SignalNoResched();
  }
}

}  // namespace lockdep

#endif
