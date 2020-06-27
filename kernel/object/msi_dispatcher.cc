// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <platform.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <array>
#include <cstring>

#include <dev/interrupt.h>
#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <object/dispatcher.h>
#include <object/interrupt_dispatcher.h>
#include <object/msi_dispatcher.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_msi_create_count, "msi_dispatcher.create")
KCOUNTER(dispatcher_msi_interrupt_count, "msi_dispatcher.interrupts")
KCOUNTER(dispatcher_msi_mask_count, "msi_dispatcher.mask")
KCOUNTER(dispatcher_msi_unmask_count, "msi_dispatcher.unmask")
KCOUNTER(dispatcher_msi_destroy_count, "msi_dispatcher.destroy")

// Creates an a derived MsiDispatcher determined by the flags passed in
zx_status_t MsiDispatcher::Create(fbl::RefPtr<MsiAllocation> alloc, uint32_t msi_id,
                                  const fbl::RefPtr<VmObject>& vmo, zx_paddr_t cap_offset,
                                  uint32_t options, zx_rights_t* out_rights,
                                  KernelHandle<InterruptDispatcher>* out_interrupt,
                                  RegisterIntFn register_int_fn) {
  if (!out_rights || !out_interrupt ||
      (vmo->is_paged() && (vmo->is_resizable() || !vmo->is_contiguous())) ||
      cap_offset >= vmo->size() || options != 0 ||
      vmo->GetMappingCachePolicy() != ZX_CACHE_POLICY_UNCACHED_DEVICE) {
    LTRACEF(
        "out_rights = %p, out_interrupt = %p\nvmo: %s, %s, %s\nsize = %lu, "
        "cap_offset = %lu, options = %#x, cache policy = %u\n",
        out_rights, out_interrupt, vmo->is_paged() ? "paged" : "physical",
        vmo->is_contiguous() ? "contiguous" : "not contiguous",
        vmo->is_resizable() ? "resizable" : "not resizable", vmo->size(), cap_offset, options,
        vmo->GetMappingCachePolicy());
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t base_irq_id = 0;
  {
    Guard<SpinLock, IrqSave> guard{&alloc->lock()};
    if (msi_id >= alloc->block().num_irq) {
      LTRACEF("msi_id %u is out of range for the block (num_irqs: %u)\n", msi_id,
              alloc->block().num_irq);
      return ZX_ERR_INVALID_ARGS;
    }
    base_irq_id = alloc->block().base_irq_id;
  }

  zx_status_t st = alloc->ReserveId(msi_id);
  if (st != ZX_OK) {
    LTRACEF("failed to reserve msi_id %u: %d\n", msi_id, st);
    return st;
  }
  auto cleanup = fbl::MakeAutoCall([alloc, msi_id]() { alloc->ReleaseId(msi_id); });

  // To handle MSI masking we need to create a kernel mapping for the VMO handed
  // to us, this will provide access to the register controlling the given MSI.
  // The VMO must be a contiguous VMO with the cache policy already configured.
  // Size checks will come into play when we know what type of capability we're
  // working with.
  auto vmar = VmAspace::kernel_aspace()->RootVmar();
  uint32_t vector = base_irq_id + msi_id;
  ktl::array<char, ZX_MAX_NAME_LEN> name{};
  snprintf(name.data(), name.max_size(), "msi id %u (vector %u)", msi_id, vector);
  fbl::RefPtr<VmMapping> mapping;
  st = vmar->CreateVmMapping(0, vmo->size(), 0, 0, vmo, 0,
                             ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, name.data(),
                             &mapping);
  if (st != ZX_OK) {
    LTRACEF("Failed to create MSI mapping: %d\n", st);
    return st;
  }

  st = mapping->MapRange(0, vmo->size(), true);
  if (st != ZX_OK) {
    LTRACEF("Falled to MapRange for the mapping: %d\n", st);
    return st;
  }

  LTRACEF("Mapping mapped at %#lx, size %zx, vmo size %lx, cap_offset = %#lx\n", mapping->base(),
          mapping->size(), vmo->size(), cap_offset);
  fbl::AllocChecker ac;
  fbl::RefPtr<MsiDispatcher> disp;
  auto* cap = reinterpret_cast<MsiCapability*>(mapping->base() + cap_offset);
  // For the moment we only support MSI, but when MSI-X is added this object creation will
  // be extended to return a derived type suitable for MSI-X operation.
  switch (cap->id) {
    case kMsiCapabilityId: {
      // MSI capabilities fit within a given device's configuration space which is either 256
      // or 4096 bytes. But in most cases the VMO containing config space is going to be at
      // least the size of a full PCI bus's worth of devices, and physical VMOs cannot be sliced
      // into children. We can validate that the capability fits within the offset given, but
      // otherwise cannot rely on the VMO's size for validation.
      size_t add_result = 0;
      if (add_overflow(cap_offset, sizeof(MsiCapability), &add_result) ||
          add_result > vmo->size()) {
        return ZX_ERR_INVALID_ARGS;
      }

      uint16_t ctrl_val = cap->control;
      bool has_pvm = !!(ctrl_val & kMsiPvmSupported);
      bool has_64bit = !!(ctrl_val & kMsi64bitSupported);
      disp = fbl::AdoptRef<MsiDispatcher>(new (&ac) MsiDispatcherImpl(
          ktl::move(alloc), base_irq_id, msi_id, ktl::move(mapping), cap_offset,
          /* has_cap_pvm= */ has_pvm, /* has_64bit= */ has_64bit, register_int_fn));
    } break;
    default:
      LTRACEF("exiting due to unsupported MSI type.\n");
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (!ac.check()) {
    LTRACEF("Failed to allocate MsiDispatcher\n");
    return ZX_ERR_NO_MEMORY;
  }
  // If we allocated MsiDispatcher successfully then its dtor will release
  // the id if necessary.
  cleanup.cancel();

  // MSI / MSI-X interrupts share a masking approach and should be masked while
  // being serviced and unmasked while waiting for an interrupt message to arrive.
  disp->set_flags(INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT | options);

  // Mask the interrupt until it is needed.
  disp->MaskInterrupt();
  st = disp->RegisterInterruptHandler();
  if (st != ZX_OK) {
    LTRACEF("Failed to register interrupt handler for msi id %u (vector %u): %d\n", msi_id, vector,
            st);
    return st;
  }

  *out_rights = default_rights();
  out_interrupt->reset(ktl::move(disp));
  LTRACEF("MsiDispatcher successfully created.\n");
  return ZX_OK;
}

MsiDispatcher::MsiDispatcher(fbl::RefPtr<MsiAllocation>&& alloc, fbl::RefPtr<VmMapping>&& mapping,
                             uint32_t base_irq_id, uint32_t msi_id, RegisterIntFn register_int_fn)
    : alloc_(ktl::move(alloc)),
      mapping_(ktl::move(mapping)),
      register_int_fn_(register_int_fn),
      base_irq_id_(base_irq_id),
      msi_id_(msi_id) {
  kcounter_add(dispatcher_msi_create_count, 1);
}

MsiDispatcher::~MsiDispatcher() {
  zx_status_t st = alloc_->ReleaseId(msi_id_);
  if (st != ZX_OK) {
    LTRACEF("MsiDispatcher: Failed to release MSI id %u (vector %u): %d\n", msi_id_,
            base_irq_id_ + msi_id_, st);
  }
  LTRACEF("MsiDispatcher: cleaning up MSI id %u\n", msi_id_);
  kcounter_add(dispatcher_msi_destroy_count, 1);
}

// This IrqHandler acts as a trampoline to call into the base
// InterruptDispatcher's InterruptHandler() routine. Masking and signaling will
// be handled there based on flags set in the constructor.
interrupt_eoi MsiDispatcher::IrqHandler(void* ctx) {
  auto* self = reinterpret_cast<MsiDispatcher*>(ctx);
  self->InterruptHandler();
  kcounter_add(dispatcher_msi_interrupt_count, 1);
  return IRQ_EOI_DEACTIVATE;
}

zx_status_t MsiDispatcher::RegisterInterruptHandler() {
  Guard<SpinLock, IrqSave> guard{&alloc_->lock()};
  register_int_fn_(&alloc_->block(), msi_id_, IrqHandler, this);
  return ZX_OK;
}

void MsiDispatcher::UnregisterInterruptHandler() {
  Guard<SpinLock, IrqSave> guard{&alloc_->lock()};
  register_int_fn_(&alloc_->block(), msi_id_, nullptr, this);
}

void MsiDispatcherImpl::MaskInterrupt() {
  kcounter_add(dispatcher_msi_mask_count, 1);

  Guard<SpinLock, IrqSave> guard{&allocation()->lock()};
  if (has_platform_pvm_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), true);
  }

  if (has_cap_pvm_) {
    *mask_bits_reg_ |= (1 << msi_id());
    arch::DeviceMemoryBarrier();
  }
}

void MsiDispatcherImpl::UnmaskInterrupt() {
  kcounter_add(dispatcher_msi_unmask_count, 1);

  Guard<SpinLock, IrqSave> guard{&allocation()->lock()};
  if (has_platform_pvm_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), false);
  }

  if (has_cap_pvm_) {
    *mask_bits_reg_ &= ~(1 << msi_id());
    arch::DeviceMemoryBarrier();
  }
}

void MsiDispatcherImpl::DeactivateInterrupt() { }
