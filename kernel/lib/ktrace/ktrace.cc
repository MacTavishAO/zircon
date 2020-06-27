// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <lib/cmdline.h>
#include <ktl/atomic.h>
#include <lib/ktrace.h>
#include <lib/ktrace/string_ref.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <string.h>

#include <arch/ops.h>
#include <arch/user_copy.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/ktrace.h>
#include <ktl/iterator.h>
#include <lk/init.h>
#include <object/thread_dispatcher.h>
#include <vm/vm_aspace.h>

#define ktrace_ticks_per_ms() (ticks_per_second() / 1000)

namespace {

// One of these macros is invoked by kernel.inc for each syscall.

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) [ZX_SYS_##name] = #name,
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

constexpr const char* kSyscallNames[] = {
#include <lib/syscalls/kernel.inc>
};

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL

void ktrace_report_syscalls() {
  for (uint32_t i = 0; i < ktl::size(kSyscallNames); ++i) {
    ktrace_name_etc(TAG_SYSCALL_NAME, i, 0, kSyscallNames[i], true);
  }
}

}  // namespace

static StringRef* ktrace_find_probe(const char* name) {
  for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
    if (!strcmp(name, ref->string)) {
      return ref;
    }
  }
  return nullptr;
}

static void ktrace_add_probe(StringRef* string_ref) {
  // Register and emit the string ref.
  string_ref->GetId();
}

static void ktrace_report_probes(void) {
  for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
    ktrace_name_etc(TAG_PROBE_NAME, ref->id, 0, ref->string, true);
  }
}

typedef struct ktrace_state {
  // where the next record will be written
  ktl::atomic<uint32_t> offset;

  // mask of groups we allow, 0 == tracing disabled
  ktl::atomic<int> grpmask;

  // total size of the trace buffer
  uint32_t bufsize;

  // offset where tracing was stopped, 0 if tracing active
  uint32_t marker;

  // raw trace buffer
  uint8_t* buffer;

  // buffer is full or not
  ktl::atomic<bool> buffer_full;
} ktrace_state_t;

static ktrace_state_t KTRACE_STATE;

ssize_t ktrace_read_user(void* ptr, uint32_t off, size_t len) {
  ktrace_state_t* ks = &KTRACE_STATE;

  // Buffer size is limited by the marker if set,
  // otherwise limited by offset (last written point).
  // Offset can end up pointing past the end, so clip
  // it to the actual buffer size to be safe.
  uint32_t max;
  if (ks->marker) {
    max = ks->marker;
  } else {
    max = ks->offset.load();
    if (max > ks->bufsize) {
      max = ks->bufsize;
    }
  }

  // null read is a query for trace buffer size
  if (ptr == nullptr) {
    return max;
  }

  // constrain read to available buffer
  if (off >= max) {
    return 0;
  }
  if (len > (max - off)) {
    len = max - off;
  }

  if (arch_copy_to_user(ptr, ks->buffer + off, len) != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }
  return len;
}

zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr) {
  ktrace_state_t* ks = &KTRACE_STATE;

  switch (action) {
    case KTRACE_ACTION_START:
      options = KTRACE_GRP_TO_MASK(options);
      ks->marker = 0;
      ks->grpmask.store(options ? options : KTRACE_GRP_TO_MASK(KTRACE_GRP_ALL));
      ktrace_report_live_processes();
      ktrace_report_live_threads();
      break;

    case KTRACE_ACTION_STOP: {
      ks->grpmask.store(0);
      uint32_t n = ks->offset.load();
      if (n > ks->bufsize) {
        ks->marker = ks->bufsize;
      } else {
        ks->marker = n;
      }
      break;
    }

    case KTRACE_ACTION_REWIND:
      // roll back to just after the metadata
      ks->offset.store(KTRACE_RECSIZE * 2);
      ks->buffer_full.store(false);
      ktrace_report_syscalls();
      ktrace_report_probes();
      ktrace_report_vcpu_meta();
      break;

    case KTRACE_ACTION_NEW_PROBE: {
      const char* const string_in = static_cast<const char*>(ptr);

      StringRef* ref = ktrace_find_probe(string_in);
      if (ref != nullptr) {
        return ref->id;
      }

      struct DynamicStringRef {
        DynamicStringRef(const char* string) : string_ref{storage} {
          memcpy(storage, string, sizeof(storage));
        }

        StringRef string_ref;
        char storage[ZX_MAX_NAME_LEN];
      };

      // TODO(eieio,dje): Figure out how to constrain this to prevent abuse by
      // creating huge numbers of unique probes.
      fbl::AllocChecker alloc_checker;
      DynamicStringRef* dynamic_ref = new (&alloc_checker) DynamicStringRef{string_in};
      if (!alloc_checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }

      ktrace_add_probe(&dynamic_ref->string_ref);
      return dynamic_ref->string_ref.id;
    }

    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void ktrace_init(unsigned level) {
  ktrace_state_t* ks = &KTRACE_STATE;

  // There's no utility in setting up ktrace if there's no syscalls to access
  // it. See zircon/kernel/syscalls/debug.cc for the corresponding syscalls.
  // Note that because KTRACE_STATE grpmask starts at 0 and will not be changed,
  // the other functions in this file need not check for enabled-ness manually.
  bool syscalls_enabled = gCmdline.GetBool("kernel.enable-debugging-syscalls", false);

  uint32_t mb = gCmdline.GetUInt32("ktrace.bufsize", KTRACE_DEFAULT_BUFSIZE);
  uint32_t grpmask = gCmdline.GetUInt32("ktrace.grpmask", KTRACE_DEFAULT_GRPMASK);

  if (mb == 0 || !syscalls_enabled) {
    dprintf(INFO, "ktrace: disabled\n");
    return;
  }

  mb *= (1024 * 1024);

  zx_status_t status;
  VmAspace* aspace = VmAspace::kernel_aspace();
  if ((status = aspace->Alloc("ktrace", mb, reinterpret_cast<void**>(&ks->buffer), 0,
                              VmAspace::VMM_FLAG_COMMIT,
                              ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE)) < 0) {
    dprintf(INFO, "ktrace: cannot alloc buffer %d\n", status);
    return;
  }

  // The last packet written can overhang the end of the buffer,
  // so we reduce the reported size by the max size of a record
  ks->bufsize = mb - 256;
  ks->buffer_full.store(false);

  dprintf(INFO, "ktrace: buffer at %p (%u bytes)\n", ks->buffer, mb);

  // write metadata to the first two event slots
  uint64_t n = ktrace_ticks_per_ms();
  ktrace_rec_32b_t* rec = reinterpret_cast<ktrace_rec_32b_t*>(ks->buffer);
  rec[0].tag = TAG_VERSION;
  rec[0].a = KTRACE_VERSION;
  rec[1].tag = TAG_TICKS_PER_MS;
  rec[1].a = static_cast<uint32_t>(n);
  rec[1].b = static_cast<uint32_t>(n >> 32);

  // enable tracing
  ks->offset.store(KTRACE_RECSIZE * 2);
  ktrace_report_syscalls();
  ktrace_report_probes();
  ks->grpmask.store(KTRACE_GRP_TO_MASK(grpmask));

  // report names of existing threads
  ktrace_report_live_threads();

  // report metadata for VCPUs
  ktrace_report_vcpu_meta();

  // Report an event for "tracing is all set up now".  This also
  // serves to ensure that there will be at least one static probe
  // entry so that the __{start,stop}_ktrace_probe symbols above
  // will be defined by the linker.
  ktrace_probe(TraceAlways, TraceContext::Thread, "ktrace_ready"_stringref);
}

static inline bool ktrace_enabled(uint32_t tag, ktrace_state_t* ks) {
  if (tag & ks->grpmask.load())
    return true;
  return false;
}

static inline void ktrace_disable(ktrace_state_t* ks) {
  ks->grpmask.store(0);
  ks->buffer_full.store(true);
}

void ktrace_tiny(uint32_t tag, uint32_t arg) {
  ktrace_state_t* ks = &KTRACE_STATE;
  if (ktrace_enabled(tag, ks)) {
    tag = (tag & 0xFFFFFFF0) | 2;
    uint32_t off;
    if ((off = (ks->offset.fetch_add(KTRACE_HDRSIZE))) >= (ks->bufsize)) {
      // if we arrive at the end, stop
      ktrace_disable(ks);
    } else {
      ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(ks->buffer + off);
      hdr->ts = ktrace_timestamp();
      hdr->tag = tag;
      hdr->tid = arg;
    }
  }
}

void* ktrace_open(uint32_t tag, uint64_t ts) {
  ktrace_state_t* ks = &KTRACE_STATE;
  if (!ktrace_enabled(tag, ks))
    return nullptr;

  uint32_t off;
  if ((off = ks->offset.fetch_add(KTRACE_LEN(tag))) >= (ks->bufsize)) {
    // if we arrive at the end, stop
    ktrace_disable(ks);
    return nullptr;
  }

  ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(ks->buffer + off);
  hdr->ts = ts;
  hdr->tag = tag;
  hdr->tid = KTRACE_FLAGS(tag) & KTRACE_FLAGS_CPU
                 ? arch_curr_cpu_num()
                 : static_cast<uint32_t>(Thread::Current::Get()->user_tid_);
  return hdr + 1;
}

void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always) {
  ktrace_state_t* ks = &KTRACE_STATE;
  if (ktrace_enabled(tag, ks) || (always && !ks->buffer_full.load())) {
    const uint32_t len = static_cast<uint32_t>(strnlen(name, ZX_MAX_NAME_LEN - 1));

    // set size to: sizeof(hdr) + len + 1, round up to multiple of 8
    tag = (tag & 0xFFFFFFF0) | ((KTRACE_NAMESIZE + len + 1 + 7) >> 3);

    uint32_t off;
    if ((off = ks->offset.fetch_add(KTRACE_LEN(tag))) >= (ks->bufsize)) {
      // if we arrive at the end, stop
      ktrace_disable(ks);
    } else {
      ktrace_rec_name_t* rec = reinterpret_cast<ktrace_rec_name_t*>(ks->buffer + off);
      rec->tag = tag;
      rec->id = id;
      rec->arg = arg;
      memcpy(rec->name, name, len);
      rec->name[len] = 0;
    }
  }
}

// Finish initialization before starting userspace (i.e. before debug syscalls can occur).
LK_INIT_HOOK(ktrace, ktrace_init, LK_INIT_LEVEL_USER - 1)
