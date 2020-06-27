// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/pci/pio.h>
#include <trace.h>

#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <dev/pcie_root.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <kernel/mutex.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/move.h>
#include <vm/vm_aspace.h>

/* TODO(johngro) : figure this out someday.
 *
 * In theory, BARs which map PIO regions for devices are supposed to be able to
 * use bits [2, 31] to describe the programmable section of the PIO window.  On
 * real x86/64 systems, however, using the write-1s-readback technique to
 * determine programmable bits of the BAR's address (and therefor the size of the
 * I/O window) shows that the upper 16 bits are not programmable.  This makes
 * sense for x86 (where I/O space is only 16-bits), but fools the system into
 * thinking that the I/O window is enormous.
 *
 * For now, just define a mask which can be used during PIO window space
 * calculations which limits the size to 16 bits for x86/64 systems.  non-x86
 * systems are still free to use all of the bits for their PIO addresses
 * (although, it is still a bit unclear what it would mean to generate an IO
 * space cycle on an architecture which has no such thing as IO space).
 */
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_SLAB_SIZE;
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_MAX_MEM;

fbl::RefPtr<PcieBusDriver> PcieBusDriver::driver_;

PcieBusDriver::PcieBusDriver(PciePlatformInterface& platform) : platform_(platform) {}
PcieBusDriver::~PcieBusDriver() {
  // TODO(johngro): For now, if the bus driver is shutting down and unloading,
  // ASSERT that there are no currently claimed devices out there.  In the
  // long run, we need to gracefully handle disconnecting from all user mode
  // drivers (probably using a simulated hot-unplug) if we unload the bus
  // driver.
  ForeachDevice(
      [](const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
        DEBUG_ASSERT(dev);
        return true;
      },
      nullptr);

  /* Shut off all of our IRQs and free all of our bookkeeping */
  ShutdownIrqs();

  // Free the device tree
  ForeachRoot(
      [](const fbl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
        root->UnplugDownstream();
        return true;
      },
      nullptr);
  roots_.clear();

  // Release the region bookkeeping memory.
  region_bookkeeping_.reset();
}

zx_status_t PcieBusDriver::AddRoot(fbl::RefPtr<PcieRoot>&& root) {
  if (root == nullptr)
    return ZX_ERR_INVALID_ARGS;

  // Make sure that we are not already started.
  if (!IsNotStarted()) {
    TRACEF("Cannot add more PCIe roots once the bus driver has been started!\n");
    return ZX_ERR_BAD_STATE;
  }

  // Attempt to add it to the collection of roots.
  {
    Guard<Mutex> guard{&bus_topology_lock_};
    if (!roots_.insert_or_find(ktl::move(root))) {
      TRACEF("Failed to add PCIe root for bus %u, root already exists!\n", root->managed_bus_id());
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  return ZX_OK;
}

zx_status_t PcieBusDriver::SetAddressTranslationProvider(
    ktl::unique_ptr<PcieAddressProvider> provider) {
  if (!IsNotStarted()) {
    TRACEF("Cannot set an address provider if the driver is already running\n");
    return ZX_ERR_BAD_STATE;
  }

  if (provider == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  addr_provider_ = ktl::move(provider);

  return ZX_OK;
}

zx_status_t PcieBusDriver::RescanDevices() {
  if (!IsOperational()) {
    TRACEF("Cannot rescan devices until the bus driver is operational!\n");
    return ZX_ERR_BAD_STATE;
  }

  Guard<Mutex> guard{&bus_rescan_lock_};

  // Scan each root looking for for devices and other bridges.
  ForeachRoot(
      [](const fbl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
        root->ScanDownstream();
        return true;
      },
      nullptr);

  // Attempt to allocate any unallocated BARs
  ForeachRoot(
      [](const fbl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
        root->AllocateDownstreamBars();
        return true;
      },
      nullptr);

  return ZX_OK;
}

bool PcieBusDriver::IsNotStarted(bool allow_quirks_phase) const {
  Guard<Mutex> guard{&start_lock_};

  if ((state_ != State::NOT_STARTED) &&
      (!allow_quirks_phase || (state_ != State::STARTING_RUNNING_QUIRKS)))
    return false;

  return true;
}

bool PcieBusDriver::AdvanceState(State expected, State next) {
  Guard<Mutex> guard{&start_lock_};

  if (state_ != expected) {
    TRACEF(
        "Failed to advance PCIe bus driver state to %u.  "
        "Expected state (%u) does not match current state (%u)\n",
        static_cast<uint>(next), static_cast<uint>(expected), static_cast<uint>(state_));
    return false;
  }

  state_ = next;
  return true;
}

zx_status_t PcieBusDriver::StartBusDriver() {
  if (!AdvanceState(State::NOT_STARTED, State::STARTING_SCANNING))
    return ZX_ERR_BAD_STATE;

  {
    Guard<Mutex> guard{&bus_rescan_lock_};

    // Scan each root looking for for devices and other bridges.
    ForeachRoot(
        [](const fbl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
          root->ScanDownstream();
          return true;
        },
        nullptr);

    if (!AdvanceState(State::STARTING_SCANNING, State::STARTING_RUNNING_QUIRKS))
      return ZX_ERR_BAD_STATE;

    // Run registered quirk handlers for any newly discovered devices.
    ForeachDevice(
        [](const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
          PcieBusDriver::RunQuirks(dev);
          return true;
        },
        nullptr);

    // Indicate to the registered quirks handlers that we are finished with the
    // quirks phase.
    PcieBusDriver::RunQuirks(nullptr);

    if (!AdvanceState(State::STARTING_RUNNING_QUIRKS, State::STARTING_RESOURCE_ALLOCATION))
      return ZX_ERR_BAD_STATE;

    // Attempt to allocate any unallocated BARs
    ForeachRoot(
        [](const fbl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
          root->AllocateDownstreamBars();
          return true;
        },
        nullptr);
  }

  if (!AdvanceState(State::STARTING_RESOURCE_ALLOCATION, State::OPERATIONAL))
    return ZX_ERR_BAD_STATE;

  return ZX_OK;
}

fbl::RefPtr<PcieDevice> PcieBusDriver::GetNthDevice(uint32_t index) {
  struct GetNthDeviceState {
    uint32_t index;
    fbl::RefPtr<PcieDevice> ret;
  } state;

  state.index = index;

  ForeachDevice(
      [](const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
        DEBUG_ASSERT(dev && ctx);

        auto state = reinterpret_cast<GetNthDeviceState*>(ctx);
        if (!state->index) {
          state->ret = dev;
          return false;
        }

        state->index--;
        return true;
      },
      &state);

  return ktl::move(state.ret);
}

void PcieBusDriver::LinkDeviceToUpstream(PcieDevice& dev, PcieUpstreamNode& upstream) {
  Guard<Mutex> guard{&bus_topology_lock_};

  // Have the device hold a reference to its upstream bridge.
  DEBUG_ASSERT(dev.upstream_ == nullptr);
  dev.upstream_ = fbl::RefPtr(&upstream);

  // Have the bridge hold a reference to the device
  uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
  DEBUG_ASSERT(ndx < ktl::size(upstream.downstream_));
  DEBUG_ASSERT(upstream.downstream_[ndx] == nullptr);
  upstream.downstream_[ndx] = fbl::RefPtr(&dev);
}

void PcieBusDriver::UnlinkDeviceFromUpstream(PcieDevice& dev) {
  Guard<Mutex> guard{&bus_topology_lock_};

  if (dev.upstream_ != nullptr) {
    uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
    DEBUG_ASSERT(ndx < ktl::size(dev.upstream_->downstream_));
    DEBUG_ASSERT(&dev == dev.upstream_->downstream_[ndx].get());

    // Let go of the upstream's reference to the device
    dev.upstream_->downstream_[ndx] = nullptr;

    // Let go of the device's reference to its upstream
    dev.upstream_ = nullptr;
  }
}

fbl::RefPtr<PcieUpstreamNode> PcieBusDriver::GetUpstream(PcieDevice& dev) {
  Guard<Mutex> guard{&bus_topology_lock_};
  auto ret = dev.upstream_;
  return ret;
}

fbl::RefPtr<PcieDevice> PcieBusDriver::GetDownstream(PcieUpstreamNode& upstream, uint ndx) {
  DEBUG_ASSERT(ndx <= ktl::size(upstream.downstream_));
  Guard<Mutex> guard{&bus_topology_lock_};
  auto ret = upstream.downstream_[ndx];
  return ret;
}

fbl::RefPtr<PcieDevice> PcieBusDriver::GetRefedDevice(uint bus_id, uint dev_id, uint func_id) {
  struct GetRefedDeviceState {
    uint bus_id;
    uint dev_id;
    uint func_id;
    fbl::RefPtr<PcieDevice> ret;
  } state;

  state.bus_id = bus_id, state.dev_id = dev_id, state.func_id = func_id,

  ForeachDevice(
      [](const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
        DEBUG_ASSERT(dev && ctx);
        auto state = reinterpret_cast<GetRefedDeviceState*>(ctx);

        if ((state->bus_id == dev->bus_id()) && (state->dev_id == dev->dev_id()) &&
            (state->func_id == dev->func_id())) {
          state->ret = dev;
          return false;
        }

        return true;
      },
      &state);

  return ktl::move(state.ret);
}

void PcieBusDriver::ForeachRoot(ForeachRootCallback cbk, void* ctx) {
  DEBUG_ASSERT(cbk);

  // Iterate over the roots, calling the registered callback for each one.
  // Hold a reference to each root while we do this, but do not hold the
  // topology lock.  Note that this requires some slightly special handling
  // when it comes to advancing the iterator as the root we are holding the
  // reference to could (in theory) be removed from the collection during the
  // callback..
  Guard<Mutex> guard{&bus_topology_lock_};

  auto iter = roots_.begin();
  bool keep_going = true;
  while (iter.IsValid()) {
    // Grab our ref.
    auto root_ref = iter.CopyPointer();

    // Perform our callback.
    guard.CallUnlocked([&keep_going, &cbk, &root_ref, &ctx] { keep_going = cbk(root_ref, ctx); });
    if (!keep_going) {
      break;
    }

    // If the root is still in the collection, simply advance the iterator.
    // Otherwise, find the root (if any) with the next higher managed bus
    // id.
    if (root_ref->InContainer()) {
      ++iter;
    } else {
      iter = roots_.upper_bound(root_ref->GetKey());
    }
  }
}

void PcieBusDriver::ForeachDevice(ForeachDeviceCallback cbk, void* ctx) {
  DEBUG_ASSERT(cbk);

  struct ForeachDeviceCtx {
    PcieBusDriver* driver;
    ForeachDeviceCallback dev_cbk;
    void* dev_ctx;
  };

  ForeachDeviceCtx foreach_device_ctx = {
      .driver = this,
      .dev_cbk = cbk,
      .dev_ctx = ctx,
  };

  ForeachRoot(
      [](const fbl::RefPtr<PcieRoot>& root, void* ctx_) -> bool {
        auto ctx = static_cast<ForeachDeviceCtx*>(ctx_);
        return ctx->driver->ForeachDownstreamDevice(root, 0, ctx->dev_cbk, ctx->dev_ctx);
      },
      &foreach_device_ctx);
}

zx_status_t PcieBusDriver::AllocBookkeeping() {
  // Create the RegionPool we will use to supply the memory for the
  // bookkeeping for all of our region tracking and allocation needs.  Then
  // assign it to each of our allocators.
  region_bookkeeping_ = RegionAllocator::RegionPool::Create(REGION_BOOKKEEPING_MAX_MEM);
  if (region_bookkeeping_ == nullptr) {
    TRACEF("Failed to create pool allocator for Region bookkeeping!\n");
    return ZX_ERR_NO_MEMORY;
  }

  mmio_lo_regions_.SetRegionPool(region_bookkeeping_);
  mmio_hi_regions_.SetRegionPool(region_bookkeeping_);
  pio_regions_.SetRegionPool(region_bookkeeping_);

  return ZX_OK;
}

bool PcieBusDriver::ForeachDownstreamDevice(const fbl::RefPtr<PcieUpstreamNode>& upstream,
                                            uint level, ForeachDeviceCallback cbk, void* ctx) {
  DEBUG_ASSERT(upstream && cbk);
  bool keep_going = true;

  for (uint i = 0; keep_going && (i < ktl::size(upstream->downstream_)); ++i) {
    auto dev = upstream->GetDownstream(i);

    if (!dev)
      continue;

    keep_going = cbk(dev, ctx, level);

    // It should be impossible to have a bridge topology such that we could
    // recurse more than 256 times.
    if (keep_going && (level < 256)) {
      if (dev->is_bridge()) {
        // TODO(johngro): eliminate the need to hold this extra ref.  If
        // we had the ability to up and downcast when moving RefPtrs, we
        // could just ktl::move dev into a PcieBridge pointer and then
        // down into a PcieUpstreamNode pointer.
        fbl::RefPtr<PcieUpstreamNode> downstream_bridge(
            static_cast<PcieUpstreamNode*>(static_cast<PcieBridge*>(dev.get())));
        keep_going = ForeachDownstreamDevice(downstream_bridge, level + 1, cbk, ctx);
      }
    }
  }

  return keep_going;
}

zx_status_t PcieBusDriver::AddSubtractBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace,
                                                bool add_op) {
  if (!IsNotStarted(true)) {
    TRACEF("Cannot add/subtract bus regions once the bus driver has been started!\n");
    return ZX_ERR_BAD_STATE;
  }

  if (!size)
    return ZX_ERR_INVALID_ARGS;

  uint64_t end = base + size - 1;
  auto OpPtr = add_op ? &RegionAllocator::AddRegion : &RegionAllocator::SubtractRegion;

  if (aspace == PciAddrSpace::MMIO) {
    // Figure out if this goes in the low region, the high region, or needs
    // to be split into two regions.
    constexpr uint64_t U32_MAX = ktl::numeric_limits<uint32_t>::max();
    auto& mmio_lo = mmio_lo_regions_;
    auto& mmio_hi = mmio_hi_regions_;

    if (end <= U32_MAX) {
      return (mmio_lo.*OpPtr)({.base = base, .size = size}, true);
    } else if (base > U32_MAX) {
      return (mmio_hi.*OpPtr)({.base = base, .size = size}, true);
    } else {
      uint64_t lo_base = base;
      uint64_t hi_base = U32_MAX + 1;
      uint64_t lo_size = hi_base - lo_base;
      uint64_t hi_size = size - lo_size;
      zx_status_t res;

      res = (mmio_lo.*OpPtr)({.base = lo_base, .size = lo_size}, true);
      if (res != ZX_OK)
        return res;

      return (mmio_hi.*OpPtr)({.base = hi_base, .size = hi_size}, true);
    }
  } else {
    DEBUG_ASSERT(aspace == PciAddrSpace::PIO);

    if ((base | end) & ~PCIE_PIO_ADDR_SPACE_MASK)
      return ZX_ERR_INVALID_ARGS;

    return (pio_regions_.*OpPtr)({.base = base, .size = size}, true);
  }
}

zx_status_t PcieBusDriver::InitializeDriver(PciePlatformInterface& platform) {
  Guard<Mutex> guard{PcieBusDriverLock::Get()};

  if (driver_ != nullptr) {
    TRACEF("Failed to initialize PCIe bus driver; driver already initialized\n");
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  driver_ = fbl::AdoptRef(new (&ac) PcieBusDriver(platform));
  if (!ac.check()) {
    TRACEF("Failed to allocate PCIe bus driver\n");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t ret = driver_->AllocBookkeeping();
  if (ret != ZX_OK)
    driver_.reset();

  return ret;
}

void PcieBusDriver::ShutdownDriver() {
  fbl::RefPtr<PcieBusDriver> driver;

  {
    Guard<Mutex> guard{PcieBusDriverLock::Get()};
    driver = ktl::move(driver_);
  }

  driver.reset();
}

/*******************************************************************************
 *
 *  ECAM support
 *
 ******************************************************************************/
/* TODO(cja): The bus driver owns all configs as well as devices so the
 * lifecycle of both are already dependent. Should this still return a refptr?
 */
const PciConfig* PcieBusDriver::GetConfig(uint bus_id, uint dev_id, uint func_id,
                                          paddr_t* out_cfg_phys) {
  DEBUG_ASSERT(bus_id < PCIE_MAX_BUSSES);
  DEBUG_ASSERT(dev_id < PCIE_MAX_DEVICES_PER_BUS);
  DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

  if (!addr_provider_) {
    TRACEF("Cannot get state if no address translation provider is set\n");
    return nullptr;
  }

  if (out_cfg_phys) {
    *out_cfg_phys = 0;
  }

  uintptr_t addr;
  zx_status_t result =
      addr_provider_->Translate(static_cast<uint8_t>(bus_id), static_cast<uint8_t>(dev_id),
                                static_cast<uint8_t>(func_id), &addr, out_cfg_phys);
  if (result != ZX_OK) {
    return nullptr;
  }

  // Check if we already have this config space cached somewhere.
  auto cfg_iter = configs_.find_if([addr](const PciConfig& cfg) { return (cfg.base() == addr); });

  if (cfg_iter.IsValid()) {
    return &(*cfg_iter);
  }

  // Nothing found, create a new PciConfig for this address
  auto cfg = addr_provider_->CreateConfig(addr);
  configs_.push_front(cfg);

  return cfg.get();
}

// External references to the quirks handler table.
extern const PcieBusDriver::QuirkHandler pcie_quirk_handlers[];
void PcieBusDriver::RunQuirks(const fbl::RefPtr<PcieDevice>& dev) {
  if (dev && dev->quirks_done())
    return;

  for (size_t i = 0; pcie_quirk_handlers[i] != nullptr; i++) {
    pcie_quirk_handlers[i](dev);
  }

  if (dev != nullptr)
    dev->SetQuirksDone();
}

// Workaround to disable all devices on the bus for mexec. This should not be
// used for any other reason due to it intentionally leaving drivers in a bad
// state (some may crash).
// TODO(cja): The paradise serial workaround in particular may need a smarter
// way of being handled in the future because it is not uncommon to have serial
// bus devices initialized by the bios that we need to retain in zedboot/crash
// situations.
void PcieBusDriver::DisableBus() {
  Guard<Mutex> guard{PcieBusDriverLock::Get()};
  ForeachDevice(
      [](const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
        if (!dev->is_bridge() && !(dev->vendor_id() == 0x8086 && dev->device_id() == 0x9d66)) {
          TRACEF("Disabling device %#02x:%#02x.%01x - VID %#04x DID %#04x\n", dev->dev_id(),
                 dev->bus_id(), dev->func_id(), dev->vendor_id(), dev->device_id());
          dev->EnableBusMaster(false);
          dev->Disable();
        } else {
          TRACEF("Skipping LP Serial disable!");
        }
        return true;
      },
      nullptr);
}
