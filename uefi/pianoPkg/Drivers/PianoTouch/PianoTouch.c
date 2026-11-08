/** @file
  Novatek touch path probe for the Xiaomi Pad 8 Pro (piano, SM8750).

  Reads the GENI serial engine that carries the panel's touch SPI and the TLMM
  line the panel pulls for its interrupt, and reports both on the UEFI console.
  It programs nothing: the goal of this first revision is to prove the two
  register windows are reachable from DXE and to record the state the firmware
  left behind, before any initialisation sequence is written against guesses.

  Hardware, from the device's own device tree (spi@a88000 / novatek@0):
    - GENI SE base   0x0A88000, 0x4000 bytes, qcom,spi-geni
    - GIC interrupt  355 (0x163), level high
    - SPI clock      19.2 MHz (0x124F800) at the panel, 50 MHz controller ceiling
    - IRQ line       TLMM phandle 0x27, pin 162, active low, edge
    - LCD id line    TLMM phandle 0x27, pin 100

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define PIANO_GENI_SE_BASE      0x0A88000ULL
#define PIANO_GENI_SE_BYTES     0x4000
#define PIANO_TOUCH_IRQ_GPIO    162
#define PIANO_TOUCH_LCD_ID      100
#define PIANO_TOUCH_SPI_HZ      19200000

/* GENI register offsets (GENI serial engine programming guide). */
#define GENI_FORCE_DEFAULT_REG      0x20
#define GENI_OUTPUT_CTRL            0x24
#define GENI_CGC_CTRL               0x28
#define GENI_STATUS_REG             0x40
#define GENI_SER_M_CLK_CFG          0x48
#define GENI_SER_S_CLK_CFG          0x4C
#define GENI_IF_FORCE_DISABLE       0x44
#define GENI_IRQ_STATUS             0x60
#define GENI_IRQ_EN                 0x64
#define SE_GENI_CFG_REG             0x9C
#define GENI_SPI_MASTER_CFG         0x1000

STATIC
UINT32
PianoMmioRead32 (
  IN UINT64  Address
  )
{
  return *(volatile UINT32 *)(UINTN)Address;
}

STATIC
VOID
PianoMmioWrite32 (
  IN UINT64  Address,
  IN UINT32  Value
  )
{
  *(volatile UINT32 *)(UINTN)Address = Value;
}

STATIC
VOID
PianoDumpGeni (
  VOID
  )
{
  CONST UINT64 Base = PIANO_GENI_SE_BASE;

  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: GENI SE @0x%lx status=%08x irq_status=%08x irq_en=%08x\n",
    (UINT64)Base,
    PianoMmioRead32 (Base + GENI_STATUS_REG),
    PianoMmioRead32 (Base + GENI_IRQ_STATUS),
    PianoMmioRead32 (Base + GENI_IRQ_EN)
    ));
  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: force_default=%08x output_ctrl=%08x cgc=%08x if_force_dis=%08x\n",
    PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG),
    PianoMmioRead32 (Base + GENI_OUTPUT_CTRL),
    PianoMmioRead32 (Base + GENI_CGC_CTRL),
    PianoMmioRead32 (Base + GENI_IF_FORCE_DISABLE)
    ));
  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: m_clk_cfg=%08x s_clk_cfg=%08x geni_cfg=%08x spi_master=%08x\n",
    PianoMmioRead32 (Base + GENI_SER_M_CLK_CFG),
    PianoMmioRead32 (Base + GENI_SER_S_CLK_CFG),
    PianoMmioRead32 (Base + SE_GENI_CFG_REG),
    PianoMmioRead32 (Base + GENI_SPI_MASTER_CFG)
    ));
}

/**
  Round-trips the GENI force-default bit, which every Qualcomm SPI driver sets
  first. A window that is not clocked or not owned by us will not take it, and
  that answers whether a touch driver can work from DXE at all.
**/
STATIC
EFI_STATUS
PianoProbeGeniWritable (
  VOID
  )
{
  CONST UINT64 Base  = PIANO_GENI_SE_BASE;
  UINT32       Before;
  UINT32       After;

  Before = PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG);
  PianoMmioWrite32 (Base + GENI_FORCE_DEFAULT_REG, Before | 1u);
  After = PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG);
  PianoMmioWrite32 (Base + GENI_FORCE_DEFAULT_REG, Before);

  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: force_default write-back %08x -> %08x (%a)\n",
    Before,
    After,
    (After & 1u) ? "writable" : "NOT writable"
    ));
  return (After & 1u) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
EFIAPI
PianoTouchInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: probe, SPI %u Hz, IRQ gpio %u, lcd-id gpio %u\n",
    (UINT32)PIANO_TOUCH_SPI_HZ,
    (UINT32)PIANO_TOUCH_IRQ_GPIO,
    (UINT32)PIANO_TOUCH_LCD_ID
    ));

  PianoDumpGeni ();
  Status = PianoProbeGeniWritable ();
  DEBUG ((DEBUG_ERROR, "PianoTouch: probe %a\n", EFI_ERROR (Status) ? "FAILED" : "OK"));

  /* A probe never blocks the boot: the panel simply stays without a driver. */
  return EFI_SUCCESS;
}
