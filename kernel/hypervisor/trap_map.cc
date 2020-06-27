// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hypervisor/ktrace.h>
#include <hypervisor/trap_map.h>

static constexpr size_t kMaxPacketsPerRange = 256;

namespace hypervisor {

BlockingPortAllocator::BlockingPortAllocator() : semaphore_(kMaxPacketsPerRange) {}

zx_status_t BlockingPortAllocator::Init() {
  return arena_.Init("hypervisor-packets", kMaxPacketsPerRange);
}

PortPacket* BlockingPortAllocator::AllocBlocking() {
  ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_PORT);
  zx_status_t status = semaphore_.Wait(Deadline::infinite());
  ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_PORT);
  if (status != ZX_OK) {
    return nullptr;
  }
  return Alloc();
}

PortPacket* BlockingPortAllocator::Alloc() {
  return arena_.New(this /* handle */, this /* allocator */);
}

void BlockingPortAllocator::Free(PortPacket* port_packet) {
  arena_.Delete(port_packet);
  semaphore_.Post();
}

Trap::Trap(uint32_t kind, zx_gpaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
           uint64_t key)
    : kind_(kind), addr_(addr), len_(len), port_(ktl::move(port)), key_(key) {}

Trap::~Trap() {
  if (port_ == nullptr) {
    return;
  }
  port_->CancelQueued(&port_allocator_ /* handle */, key_);
}

zx_status_t Trap::Init() { return port_allocator_.Init(); }

zx_status_t Trap::Queue(const zx_port_packet_t& packet, StateInvalidator* invalidator) {
  if (invalidator != nullptr) {
    invalidator->Invalidate();
  }
  if (port_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  PortPacket* port_packet = port_allocator_.AllocBlocking();
  if (port_packet == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  port_packet->packet = packet;
  zx_status_t status = port_->Queue(port_packet, ZX_SIGNAL_NONE);
  if (status != ZX_OK) {
    port_allocator_.Free(port_packet);
    if (status == ZX_ERR_BAD_HANDLE) {
      // If the last handle to the port has been closed, then we're in a bad state.
      status = ZX_ERR_BAD_STATE;
    }
  }
  return status;
}

zx_status_t TrapMap::InsertTrap(uint32_t kind, zx_gpaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  TrapTree* traps = TreeOf(kind);
  if (traps == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto iter = traps->find(addr);
  if (iter.IsValid()) {
    dprintf(INFO,
            "Trap for kind %u (addr %#lx len %lu key %lu) already exists "
            "(addr %#lx len %lu key %lu)\n",
            kind, addr, len, key, iter->addr(), iter->len(), iter->key());
    return ZX_ERR_ALREADY_EXISTS;
  }
  fbl::AllocChecker ac;
  ktl::unique_ptr<Trap> range(new (&ac) Trap(kind, addr, len, ktl::move(port), key));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = range->Init();
  if (status != ZX_OK) {
    return status;
  }
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    traps->insert(ktl::move(range));
  }
  return ZX_OK;
}

zx_status_t TrapMap::FindTrap(uint32_t kind, zx_gpaddr_t addr, Trap** trap) {
  TrapTree* traps = TreeOf(kind);
  if (traps == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  TrapTree::iterator iter;
  {
    Guard<SpinLock, IrqSave> guard{&lock_};
    iter = traps->upper_bound(addr);
  }
  --iter;
  if (!iter.IsValid() || !iter->Contains(addr)) {
    return ZX_ERR_NOT_FOUND;
  }
  *trap = const_cast<Trap*>(&*iter);
  return ZX_OK;
}

TrapMap::TrapTree* TrapMap::TreeOf(uint32_t kind) {
  switch (kind) {
    case ZX_GUEST_TRAP_BELL:
    case ZX_GUEST_TRAP_MEM:
      return &mem_traps_;
#ifdef ARCH_X86
    case ZX_GUEST_TRAP_IO:
      return &io_traps_;
#endif  // ARCH_X86
    default:
      return nullptr;
  }
}

}  // namespace hypervisor
