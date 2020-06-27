// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <lib/crashlog.h>
#include <lib/elf-psabi/sp.h>
#include <lib/instrumentation/vmo.h>
#include <lib/userabi/rodso.h>
#include <lib/userabi/userboot.h>
#include <lib/userabi/vdso.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <mexec.h>
#include <platform.h>
#include <stdio.h>
#include <trace.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <lk/init.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <platform/crashlog.h>
#include <vm/vm_object_paged.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST
#include <lib/crypto/entropy/quality_test.h>
#endif

static_assert(userboot::kCmdlineMax == Cmdline::kCmdlineMax);

namespace {

using namespace userboot;

constexpr const char kStackVmoName[] = "userboot-initial-stack";
constexpr const char kCrashlogVmoName[] = "crashlog";
constexpr const char kZbiVmoName[] = "zbi";

constexpr size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;

#include "userboot-code.h"

// This is defined in assembly via RODSO_IMAGE (see rodso-asm.h);
// userboot-code.h gives details about the image's size and layout.
extern "C" const char userboot_image[];

KCOUNTER(timeline_userboot, "boot.timeline.userboot")
KCOUNTER(init_time, "init.userboot.time.msec")

class UserbootImage : private RoDso {
 public:
  UserbootImage(const VDso* vdso, KernelHandle<VmObjectDispatcher>* vmo_kernel_handle)
      : RoDso("userboot", userboot_image, USERBOOT_CODE_END, USERBOOT_CODE_START,
              vmo_kernel_handle),
        vdso_(vdso) {}

  // The whole userboot image consists of the userboot rodso image
  // immediately followed by the vDSO image.  This returns the size
  // of that combined image.
  size_t size() const { return RoDso::size() + vdso_->size(); }

  zx_status_t Map(fbl::RefPtr<VmAddressRegionDispatcher> root_vmar, uintptr_t* vdso_base,
                  uintptr_t* entry) {
    // Create a VMAR (placed anywhere) to hold the combined image.
    KernelHandle<VmAddressRegionDispatcher> vmar_handle;
    zx_rights_t vmar_rights;
    zx_status_t status = root_vmar->Allocate(
        0, size(),
        ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_EXECUTE | ZX_VM_CAN_MAP_SPECIFIC,
        &vmar_handle, &vmar_rights);
    if (status != ZX_OK)
      return status;

    // Map userboot proper.
    status = RoDso::Map(vmar_handle.dispatcher(), 0);
    if (status == ZX_OK) {
      *entry = vmar_handle.dispatcher()->vmar()->base() + USERBOOT_ENTRY;

      // Map the vDSO right after it.
      *vdso_base = vmar_handle.dispatcher()->vmar()->base() + RoDso::size();

      // Releasing |vmar_handle| is safe because it has a no-op
      // on_zero_handles(), otherwise the mapping routines would have
      // to take ownership of the handle and manage its lifecycle.
      status = vdso_->Map(vmar_handle.release(), RoDso::size());
    }
    return status;
  }

 private:
  const VDso* vdso_;
};

// Keep a global reference to the kcounters vmo so that the kcounters
// memory always remains valid, even if userspace closes the last handle.
fbl::RefPtr<VmObject> kcounters_vmo_ref;

// Get a handle to a VM object, with full rights except perhaps for writing.
zx_status_t get_vmo_handle(fbl::RefPtr<VmObject> vmo, bool readonly, uint64_t content_size,
                           fbl::RefPtr<VmObjectDispatcher>* disp_ptr, Handle** ptr) {
  if (!vmo)
    return ZX_ERR_NO_MEMORY;
  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> vmo_kernel_handle;
  zx_status_t result = VmObjectDispatcher::Create(ktl::move(vmo), &vmo_kernel_handle, &rights);
  if (result == ZX_OK) {
    vmo_kernel_handle.dispatcher()->SetContentSize(content_size);
    if (disp_ptr)
      *disp_ptr = vmo_kernel_handle.dispatcher();
    if (readonly)
      rights &= ~ZX_RIGHT_WRITE;
    if (ptr)
      *ptr = Handle::Make(ktl::move(vmo_kernel_handle), rights).release();
  }
  return result;
}

HandleOwner get_job_handle() {
  KernelHandle<JobDispatcher> handle{GetRootJobDispatcher()};
  return Handle::Make(ktl::move(handle), JobDispatcher::default_rights());
}

HandleOwner get_resource_handle() {
  zx_rights_t rights;
  KernelHandle<ResourceDispatcher> root;
  zx_status_t result =
      ResourceDispatcher::Create(&root, &rights, ZX_RSRC_KIND_ROOT, 0, 0, 0, "root");
  ASSERT(result == ZX_OK);
  return Handle::Make(ktl::move(root), rights);
}

void clog_to_vmo(const void* data, size_t off, size_t len, void* cookie) {
  VmObject* vmo = static_cast<VmObject*>(cookie);
  vmo->Write(data, off, len);
}

// Converts platform crashlog into a VMO
zx_status_t crashlog_to_vmo(fbl::RefPtr<VmObject>* out, size_t* out_size) {
  size_t size = platform_recover_crashlog(0, NULL, NULL);
  fbl::RefPtr<VmObject> crashlog_vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &crashlog_vmo);

  if (status != ZX_OK) {
    return status;
  }

  if (size) {
    platform_recover_crashlog(size, crashlog_vmo.get(), clog_to_vmo);
  }

  crashlog_vmo->set_name(kCrashlogVmoName, sizeof(kCrashlogVmoName) - 1);

  // Stash the recovered crashlog so that it may be propagated to the next
  // kernel instance in case we later mexec.
  crashlog_stash(crashlog_vmo);

  *out = ktl::move(crashlog_vmo);
  *out_size = size;

  // Now that we have recovered the old crashlog, enable crashlog uptime
  // updates.  This will cause systems with a RAM based crashlog to periodically
  // create a payload-less crashlog indicating a SW reboot reason of "unknown"
  // along with an uptime indicator.  If the system spontaneously reboots (due
  // to something like a WDT, or brownout) we will be able to recover this log
  // and know that we spontaneously rebooted, and have some idea of how long we
  // were running before we did.
  platform_enable_crashlog_uptime_updates(true);
  return ZX_OK;
}

void bootstrap_vmos(Handle** handles) {
  size_t rsize;
  void* rbase = platform_get_ramdisk(&rsize);
  if (rbase) {
    dprintf(INFO, "userboot: ramdisk %#15zx @ %p\n", rsize, rbase);
  }

  // The ZBI.
  fbl::RefPtr<VmObject> rootfs_vmo;
  zx_status_t status = VmObjectPaged::CreateFromWiredPages(rbase, rsize, true, &rootfs_vmo);
  ASSERT(status == ZX_OK);
  rootfs_vmo->set_name(kZbiVmoName, sizeof(kZbiVmoName) - 1);
  status = get_vmo_handle(rootfs_vmo, false, rsize, nullptr, &handles[kZbi]);
  ASSERT(status == ZX_OK);

  // Crashlog.
  fbl::RefPtr<VmObject> crashlog_vmo;
  size_t crashlog_size;
  status = crashlog_to_vmo(&crashlog_vmo, &crashlog_size);
  ASSERT(status == ZX_OK);
  status = get_vmo_handle(crashlog_vmo, true, crashlog_size, nullptr, &handles[kCrashlog]);
  ASSERT(status == ZX_OK);

#if ENABLE_ENTROPY_COLLECTOR_TEST
  ASSERT(!crypto::entropy::entropy_was_lost);
  status =
      get_vmo_handle(crypto::entropy::entropy_vmo, true, crypto::entropy::entropy_vmo_content_size,
                     nullptr, &handles[kEntropyTestData]);
  ASSERT(status == ZX_OK);
#endif

  // kcounters names table.
  fbl::RefPtr<VmObject> kcountdesc_vmo;
  status = VmObjectPaged::CreateFromWiredPages(CounterDesc().VmoData(), CounterDesc().VmoDataSize(),
                                               true, &kcountdesc_vmo);
  ASSERT(status == ZX_OK);
  kcountdesc_vmo->set_name(counters::DescriptorVmo::kVmoName,
                           sizeof(counters::DescriptorVmo::kVmoName) - 1);
  status = get_vmo_handle(ktl::move(kcountdesc_vmo), true, CounterDesc().VmoContentSize(), nullptr,
                          &handles[kCounterNames]);
  ASSERT(status == ZX_OK);

  // kcounters live data.
  fbl::RefPtr<VmObject> kcounters_vmo;
  status = VmObjectPaged::CreateFromWiredPages(CounterArena().VmoData(),
                                               CounterArena().VmoDataSize(), false, &kcounters_vmo);
  ASSERT(status == ZX_OK);
  kcounters_vmo_ref = kcounters_vmo;
  kcounters_vmo->set_name(counters::kArenaVmoName, sizeof(counters::kArenaVmoName) - 1);
  status = get_vmo_handle(ktl::move(kcounters_vmo), true, CounterArena().VmoContentSize(), nullptr,
                          &handles[kCounters]);
  ASSERT(status == ZX_OK);

  status = InstrumentationData::GetVmos(&handles[kFirstInstrumentationData]);
  ASSERT(status == ZX_OK);
}

void userboot_init(uint) {
  // Prepare the bootstrap message packet.  This puts its data (the
  // kernel command line) in place, and allocates space for its handles.
  // We'll fill in the handles as we create things.
  MessagePacketPtr msg;
  zx_status_t status = MessagePacket::Create(
      gCmdline.data(), static_cast<uint32_t>(gCmdline.size()), userboot::kHandleCount, &msg);
  ASSERT(status == ZX_OK);
  Handle** const handles = msg->mutable_handles();
  DEBUG_ASSERT(msg->num_handles() == userboot::kHandleCount);

  // Create the process.
  KernelHandle<ProcessDispatcher> process_handle;
  KernelHandle<VmAddressRegionDispatcher> vmar_handle;
  zx_rights_t rights, vmar_rights;
  status = ProcessDispatcher::Create(GetRootJobDispatcher(), "userboot", 0, &process_handle,
                                     &rights, &vmar_handle, &vmar_rights);
  ASSERT(status == ZX_OK);

  // It needs its own process and root VMAR handles.
  auto process = process_handle.dispatcher();
  auto vmar = vmar_handle.dispatcher();
  HandleOwner proc_handle_owner = Handle::Make(ktl::move(process_handle), rights);
  HandleOwner vmar_handle_owner = Handle::Make(ktl::move(vmar_handle), vmar_rights);
  ASSERT(proc_handle_owner);
  ASSERT(vmar_handle_owner);
  handles[userboot::kProcSelf] = proc_handle_owner.release();
  handles[userboot::kVmarRootSelf] = vmar_handle_owner.release();

  // It gets the root resource and job handles.
  handles[userboot::kRootResource] = get_resource_handle().release();
  ASSERT(handles[userboot::kRootResource]);
  handles[userboot::kRootJob] = get_job_handle().release();
  ASSERT(handles[userboot::kRootJob]);

  // It also gets many VMOs for VDSOs and other things.
  constexpr int kVariants = static_cast<int>(userboot::VdsoVariant::COUNT);
  KernelHandle<VmObjectDispatcher> vdso_kernel_handles[kVariants];
  const VDso* vdso = VDso::Create(vdso_kernel_handles);
  for (int i = 0; i < kVariants; ++i) {
    handles[kFirstVdso + i] =
        Handle::Make(ktl::move(vdso_kernel_handles[i]), vdso->vmo_rights()).release();
    ASSERT(handles[kFirstVdso + i]);
  }
  DEBUG_ASSERT(handles[kFirstVdso]->dispatcher() == vdso->vmo());
  bootstrap_vmos(handles);

  // Make the channel that will hold the message.
  KernelHandle<ChannelDispatcher> user_handle, kernel_handle;
  status = ChannelDispatcher::Create(&user_handle, &kernel_handle, &rights);
  ASSERT(status == ZX_OK);

  // Transfer it in.
  status = kernel_handle.dispatcher()->Write(ZX_KOID_INVALID, ktl::move(msg));
  ASSERT(status == ZX_OK);

  // Inject the user-side channel handle into the process.
  HandleOwner user_handle_owner = Handle::Make(ktl::move(user_handle), rights);
  ASSERT(user_handle_owner);
  zx_handle_t hv = process->MapHandleToValue(user_handle_owner);
  process->AddHandle(ktl::move(user_handle_owner));

  // Map in the userboot image along with the vDSO.
  KernelHandle<VmObjectDispatcher> userboot_vmo_kernel_handle;
  UserbootImage userboot(vdso, &userboot_vmo_kernel_handle);
  uintptr_t vdso_base = 0;
  uintptr_t entry = 0;
  status = userboot.Map(vmar, &vdso_base, &entry);
  ASSERT(status == ZX_OK);

  // Map the stack anywhere.
  uintptr_t stack_base;
  {
    fbl::RefPtr<VmObject> stack_vmo;
    status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, stack_size, &stack_vmo);
    ASSERT(status == ZX_OK);
    stack_vmo->set_name(kStackVmoName, sizeof(kStackVmoName) - 1);

    fbl::RefPtr<VmMapping> stack_mapping;
    status = vmar->Map(0, ktl::move(stack_vmo), 0, stack_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                       &stack_mapping);
    ASSERT(status == ZX_OK);
    stack_base = stack_mapping->base();
  }
  uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);

  // Create the user thread.
  fbl::RefPtr<ThreadDispatcher> thread;
  {
    KernelHandle<ThreadDispatcher> thread_handle;
    zx_rights_t rights;
    status = ThreadDispatcher::Create(ktl::move(process), 0, "userboot", &thread_handle, &rights);
    ASSERT(status == ZX_OK);
    status = thread_handle.dispatcher()->Initialize();
    ASSERT(status == ZX_OK);
    thread = thread_handle.dispatcher();
  }
  ASSERT(thread);

  // Create a root job observer, restarting the system if the root job becomes childless.
  StartRootJobObserver();

  dprintf(SPEW, "userboot: %-23s @ %#" PRIxPTR "\n", "entry point", entry);

  // Start the process's initial thread.
  auto arg1 = static_cast<uintptr_t>(hv);
  status = thread->Start(ThreadDispatcher::EntryState{entry, sp, arg1, vdso_base},
                         /* initial_thread= */ true);
  ASSERT(status == ZX_OK);

  timeline_userboot.Set(current_ticks());
  init_time.Add(current_time() / 1000000LL);
}

}  // anonymous namespace

LK_INIT_HOOK(userboot, userboot_init, LK_INIT_LEVEL_USER)
