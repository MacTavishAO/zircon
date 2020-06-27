// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PCI_INTERRUPT_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PCI_INTERRUPT_DISPATCHER_H_
#if WITH_KERNEL_PCIE

#include <sys/types.h>

#include <fbl/canary.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <object/pci_device_dispatcher.h>

class PciDeviceDispatcher;

class PciInterruptDispatcher final : public InterruptDispatcher {
 public:
  static zx_status_t Create(const fbl::RefPtr<PcieDevice>& device, uint32_t irq_id, bool maskable,
                            zx_rights_t* out_rights,
                            KernelHandle<InterruptDispatcher>* out_interrupt);

  ~PciInterruptDispatcher() final;

 protected:
  void MaskInterrupt() final;
  void UnmaskInterrupt() final;
  void DeactivateInterrupt() final;
  void UnregisterInterruptHandler() final;

 private:
  static pcie_irq_handler_retval_t IrqThunk(const PcieDevice& dev, uint irq_id, void* ctx);
  PciInterruptDispatcher(const fbl::RefPtr<PcieDevice>& device, uint32_t vector, bool maskable);
  zx_status_t RegisterInterruptHandler();

  fbl::RefPtr<PcieDevice> device_;
  const uint32_t vector_;
  const bool maskable_;
};

#endif  // if WITH_KERNEL_PCIE

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PCI_INTERRUPT_DISPATCHER_H_
