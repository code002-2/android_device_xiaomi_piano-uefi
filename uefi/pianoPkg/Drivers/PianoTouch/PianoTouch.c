/** @file
  Novatek touch path probe for the Xiaomi Pad 8 Pro (piano, SM8750).

  Reads the GENI serial engine that carries the panel's touch SPI and reports
  what it found in a file on the boot volume (\\EFI\\UniXNU\\touch-probe.txt),
  because this platform exposes no USB serial port to the host: the file is
  read back from Android with adb after the boot.

  It programs nothing but one FORCE_DEFAULT write-back test, so a failed probe
  never affects the boot.

  Hardware, from the device's own device tree (spi@a88000 / novatek@0):
    - GENI SE base   0x0A88000, 0x4000 bytes, qcom,spi-geni
    - GIC interrupt  355 (0x163), level high
    - SPI clock      19.2 MHz (0x124F800) at the panel, 50 MHz controller ceiling
    - IRQ line       TLMM phandle 0x27, pin 162, active low, edge
    - LCD id line    TLMM phandle 0x27, pin 100

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#define PIANO_GENI_SE_BASE      0x0A88000ULL
#define PIANO_TOUCH_IRQ_GPIO    162
#define PIANO_TOUCH_LCD_ID      100
#define PIANO_TOUCH_SPI_HZ      19200000
#define PIANO_TOUCH_LOG_LIMIT   4096

/* GENI register offsets (GENI serial engine programming guide). */
#define GENI_FORCE_DEFAULT_REG      0x020
#define GENI_OUTPUT_CTRL            0x024
#define GENI_CGC_CTRL               0x028
#define GENI_STATUS_REG             0x040
#define GENI_IF_FORCE_DISABLE       0x044
#define GENI_SER_M_CLK_CFG          0x048
#define GENI_SER_S_CLK_CFG          0x04C
#define GENI_IRQ_STATUS             0x060
#define GENI_IRQ_EN                 0x064
#define SE_GENI_CFG_REG             0x09C
#define GENI_SPI_MASTER_CFG         0x1000

STATIC CHAR8  mLog[PIANO_TOUCH_LOG_LIMIT];
STATIC UINTN  mLogUsed;

STATIC
VOID
LogAppend (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  UINTN    Room;

  if (mLogUsed + 1 >= PIANO_TOUCH_LOG_LIMIT) {
    return;
  }

  Room = PIANO_TOUCH_LOG_LIMIT - mLogUsed - 1;
  VA_START (Args, Format);
  mLogUsed += AsciiVSPrint (&mLog[mLogUsed], Room, Format, Args);
  VA_END (Args);
  if (mLogUsed > PIANO_TOUCH_LOG_LIMIT - 1) {
    mLogUsed = PIANO_TOUCH_LOG_LIMIT - 1;
  }
}

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

/** Writes the probe result beside the kernel on the boot volume. */
STATIC
EFI_STATUS
WriteProbeLog (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *Image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  UINTN                            Size;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (!EFI_ERROR (Status)) {
    Status = gBS->HandleProtocol (Image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  }

  if (EFI_ERROR (Status)) {
    /* The driver may be unpacked from a firmware volume: use the first mapped
     * volume, which is the ESP that carries EFI\\UniXNU. */
    Status = gBS->LocateProtocol (&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID **)&Fs);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (Root, &File, L"\\EFI\\UniXNU\\touch-probe.txt",
                       EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }

  Size   = mLogUsed;
  Status = File->Write (File, &Size, mLog);
  File->Close (File);
  Root->Close (Root);
  return Status;
}

EFI_STATUS
EFIAPI
PianoTouchInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64      Base = PIANO_GENI_SE_BASE;
  UINT32      Before;
  UINT32      After;
  EFI_STATUS  Status;

  mLogUsed = 0;
  LogAppend ("PianoTouch probe: SPI %u Hz, irq gpio %u, lcd-id gpio %u\n",
             (UINT32)PIANO_TOUCH_SPI_HZ, (UINT32)PIANO_TOUCH_IRQ_GPIO, (UINT32)PIANO_TOUCH_LCD_ID);
  LogAppend ("GENI SE base 0x%x\n", (UINT32)Base);
  LogAppend ("STATUS      %08x\n", PianoMmioRead32 (Base + GENI_STATUS_REG));
  LogAppend ("IRQ_STATUS  %08x\n", PianoMmioRead32 (Base + GENI_IRQ_STATUS));
  LogAppend ("IRQ_EN      %08x\n", PianoMmioRead32 (Base + GENI_IRQ_EN));
  LogAppend ("FORCE_DEF   %08x\n", PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG));
  LogAppend ("OUTPUT_CTRL %08x\n", PianoMmioRead32 (Base + GENI_OUTPUT_CTRL));
  LogAppend ("CGC_CTRL    %08x\n", PianoMmioRead32 (Base + GENI_CGC_CTRL));
  LogAppend ("IF_FORCE_D  %08x\n", PianoMmioRead32 (Base + GENI_IF_FORCE_DISABLE));
  LogAppend ("M_CLK_CFG   %08x\n", PianoMmioRead32 (Base + GENI_SER_M_CLK_CFG));
  LogAppend ("S_CLK_CFG   %08x\n", PianoMmioRead32 (Base + GENI_SER_S_CLK_CFG));
  LogAppend ("GENI_CFG    %08x\n", PianoMmioRead32 (Base + SE_GENI_CFG_REG));
  LogAppend ("SPI_MASTER  %08x\n", PianoMmioRead32 (Base + GENI_SPI_MASTER_CFG));

  Before = PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG);
  PianoMmioWrite32 (Base + GENI_FORCE_DEFAULT_REG, Before | 1u);
  After = PianoMmioRead32 (Base + GENI_FORCE_DEFAULT_REG);
  PianoMmioWrite32 (Base + GENI_FORCE_DEFAULT_REG, Before);
  LogAppend ("force-default write-back: %08x -> %08x (%a)\n",
             Before, After, (After & 1u) ? "writable" : "NOT writable");

  Status = WriteProbeLog (ImageHandle);
  LogAppend ("log write: %r\n", Status);

  DEBUG ((
    DEBUG_ERROR,
    "PianoTouch: probe done, log %a, force-default %a\n",
    EFI_ERROR (Status) ? "NOT written" : "written",
    (After & 1u) ? "writable" : "NOT writable"
    ));

  return EFI_SUCCESS;
}
