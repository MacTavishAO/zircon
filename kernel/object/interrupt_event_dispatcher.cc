// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/interrupt_event_dispatcher.h"

#include <lib/counters.h>
#include <platform.h>
#include <zircon/rights.h>

#include <dev/interrupt.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>

KCOUNTER(dispatcher_interrupt_event_create_count, "dispatcher.interrupt_event.create")
KCOUNTER(dispatcher_interrupt_event_destroy_count, "dispatcher.interrupt_event.destroy")

zx_status_t InterruptEventDispatcher::Create(KernelHandle<InterruptDispatcher>* handle,
                                             zx_rights_t* rights, uint32_t vector,
                                             uint32_t options) {
  if (options & ZX_INTERRUPT_VIRTUAL) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Attempt to construct the dispatcher.
  // Do not create a KernelHandle until all initialization has succeeded;
  // if an interrupt already exists on |vector| our on_zero_handles() would
  // tear down the existing interrupt when creation fails.
  fbl::AllocChecker ac;
  auto disp = fbl::AdoptRef(new (&ac) InterruptEventDispatcher(vector));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  Guard<Mutex> guard{disp->get_lock()};

  uint32_t interrupt_flags = 0;

  if (options & ~(ZX_INTERRUPT_REMAP_IRQ | ZX_INTERRUPT_MODE_MASK)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Remap the vector if we have been asked to do so.
  if (options & ZX_INTERRUPT_REMAP_IRQ) {
    vector = remap_interrupt(vector);
  }

  if (!is_valid_interrupt(vector, 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool default_mode = false;
  enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
  enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
  switch (options & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_DEFAULT:
      default_mode = true;
      break;
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      tm = IRQ_TRIGGER_MODE_EDGE;
      pol = IRQ_POLARITY_ACTIVE_LOW;
      break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      tm = IRQ_TRIGGER_MODE_EDGE;
      pol = IRQ_POLARITY_ACTIVE_HIGH;
      break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      tm = IRQ_TRIGGER_MODE_LEVEL;
      pol = IRQ_POLARITY_ACTIVE_LOW;
      interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
      break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      tm = IRQ_TRIGGER_MODE_LEVEL;
      pol = IRQ_POLARITY_ACTIVE_HIGH;
      interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  if (!default_mode) {
    zx_status_t status = configure_interrupt(vector, tm, pol);
    if (status != ZX_OK) {
      return status;
    }
  }

  zx_status_t status = disp->set_flags(interrupt_flags);
  if (status != ZX_OK) {
    return status;
  }

  // Register the interrupt
  status = disp->RegisterInterruptHandler();
  if (status != ZX_OK) {
    return status;
  }

  unmask_interrupt(vector);

  // Transfer control of the new dispatcher to the creator and we are done.
  *rights = default_rights();
  handle->reset(ktl::move(disp));

  return ZX_OK;
}

zx_status_t InterruptEventDispatcher::BindVcpu(fbl::RefPtr<VcpuDispatcher> vcpu_dispatcher) {
  Guard<SpinLock, IrqSave> guard{&spinlock_};
  if (state() == InterruptState::DESTROYED) {
    return ZX_ERR_CANCELED;
  } else if (state() == InterruptState::WAITING) {
    return ZX_ERR_BAD_STATE;
  } else if (vcpu_ == vcpu_dispatcher) {
    return ZX_OK;
  } else if (HasPort() || vcpu_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  // It is safe to register the handler before assigning |vcpu_|, as we prevent
  // any races by holding |spinlock_|.
  MaskInterrupt();
  UnregisterInterruptHandler();
  zx_status_t status = register_int_handler(vector_, VcpuIrqHandler, this);
  UnmaskInterrupt();
  if (status != ZX_OK) {
    return status;
  }

  vcpu_ = ktl::move(vcpu_dispatcher);
  return ZX_OK;
}

interrupt_eoi InterruptEventDispatcher::IrqHandler(void* ctx) {
  InterruptEventDispatcher* self = reinterpret_cast<InterruptEventDispatcher*>(ctx);

  if (self->get_flags() & INTERRUPT_MASK_POSTWAIT)
    mask_interrupt(self->vector_);

  self->InterruptHandler();
  return IRQ_EOI_DEACTIVATE;
}

interrupt_eoi InterruptEventDispatcher::VcpuIrqHandler(void* ctx) {
  InterruptEventDispatcher* self = reinterpret_cast<InterruptEventDispatcher*>(ctx);
  self->VcpuInterruptHandler();
  // Skip the EOI to allow the guest to deactivate the interrupt.
  return IRQ_EOI_PRIORITY_DROP;
}

void InterruptEventDispatcher::VcpuInterruptHandler() {
  Guard<SpinLock, IrqSave> guard{&spinlock_};
  vcpu_->PhysicalInterrupt(vector_);
}

InterruptEventDispatcher::InterruptEventDispatcher(uint32_t vector) : vector_(vector) {
  kcounter_add(dispatcher_interrupt_event_create_count, 1);
}

InterruptEventDispatcher::~InterruptEventDispatcher() {
  kcounter_add(dispatcher_interrupt_event_destroy_count, 1);
}

void InterruptEventDispatcher::MaskInterrupt() { mask_interrupt(vector_); }

void InterruptEventDispatcher::UnmaskInterrupt() { unmask_interrupt(vector_); }

void InterruptEventDispatcher::DeactivateInterrupt() {
#if __aarch64__
  // deactivate_interrupt only exist in arm64
  deactivate_interrupt(vector_);
#endif
}

zx_status_t InterruptEventDispatcher::RegisterInterruptHandler() {
  return register_int_handler(vector_, IrqHandler, this);
}

void InterruptEventDispatcher::UnregisterInterruptHandler() {
  register_int_handler(vector_, nullptr, nullptr);
}
