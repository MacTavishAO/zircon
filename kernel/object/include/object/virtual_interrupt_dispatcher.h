// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VIRTUAL_INTERRUPT_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VIRTUAL_INTERRUPT_DISPATCHER_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>

class VirtualInterruptDispatcher final : public InterruptDispatcher {
 public:
  static zx_status_t Create(KernelHandle<InterruptDispatcher>* handle, zx_rights_t* rights,
                            uint32_t options);

  ~VirtualInterruptDispatcher() final;

  VirtualInterruptDispatcher(const InterruptDispatcher&) = delete;
  VirtualInterruptDispatcher& operator=(const InterruptDispatcher&) = delete;

 protected:
  void MaskInterrupt() final;
  void UnmaskInterrupt() final;
  void DeactivateInterrupt() final;
  void UnregisterInterruptHandler() final;

 private:
  VirtualInterruptDispatcher();
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_VIRTUAL_INTERRUPT_DISPATCHER_H_
