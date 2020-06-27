// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <endian.h>
#include <lib/pci/pio.h>
#include <zircon/types.h>

#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>

// TODO: This library exists as a shim for the awkward period between bringing
// PCI legacy support online, and moving PCI to userspace. Initially, it exists
// as a kernel library that userspace accesses via syscalls so that a userspace
// process never causes a race condition with the bus driver's accesses. Later,
// all accesses will go through the library itself in userspace and the syscalls
// will no longer exist.

namespace Pci {

#ifdef ARCH_X86
#include <arch/x86.h>
static SpinLock pio_lock;

static constexpr uint16_t kPciConfigAddr = 0xCF8;
static constexpr uint16_t kPciConfigData = 0xCFC;
static constexpr uint32_t kPciCfgEnable = (1 << 31);
static constexpr uint32_t WidthMask(size_t width) {
  return (width == 32) ? 0xffffffff : (1u << width) - 1u;
}

zx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width) {
  AutoSpinLock lock(&pio_lock);

  size_t shift = (addr & 0x3) * 8u;
  if (shift + width > 32) {
    return ZX_ERR_INVALID_ARGS;
  }

  outpd(kPciConfigAddr, (addr & ~0x3) | kPciCfgEnable);
  uint32_t tmp_val = LE32(inpd(kPciConfigData));
  uint32_t width_mask = WidthMask(width);

  // Align the read to the correct offset, then mask based on byte width
  *val = (tmp_val >> shift) & width_mask;
  return ZX_OK;
}

zx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t* val,
                       size_t width) {
  return PioCfgRead(PciBdfRawAddr(bus, dev, func, offset), val, width);
}

zx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width) {
  AutoSpinLock lock(&pio_lock);

  size_t shift = (addr & 0x3) * 8u;
  if (shift + width > 32) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t width_mask = WidthMask(width);
  uint32_t write_mask = width_mask << shift;
  outpd(kPciConfigAddr, (addr & ~0x3) | kPciCfgEnable);
  uint32_t tmp_val = LE32(inpd(kPciConfigData));

  val &= width_mask;
  tmp_val &= ~write_mask;
  tmp_val |= (val << shift);
  outpd(kPciConfigData, LE32(tmp_val));

  return ZX_OK;
}

zx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val,
                        size_t width) {
  return PioCfgWrite(PciBdfRawAddr(bus, dev, func, offset), val, width);
}

#else  // not x86
zx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t* val,
                       size_t width) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val,
                        size_t width) {
  return ZX_ERR_NOT_SUPPORTED;
}

#endif  // ARCH_X86
}  // namespace Pci
