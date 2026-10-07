# Physical display work: 2026-10-07

Build ID: `physical-display-20261007-r1`.

The requested end state remains five physical GPU display drivers including
RX6600, the highest supported monitor refresh rate, HDMI sound, and external
device-matched download/persistent installation. **This end state is not yet
implemented or verified.** Bochs/QEMU support does not count as a physical driver.

## Completed work in this build

- AMD codec `1002:aa01` uses its native speaker/audio-descriptor commands instead
  of unsupported standard ELD reads. Revision 3+ uses independent left/right
  channel commands, correct PCM ramp, AMD channel allocation and HBR disabling.
  Generic HDMI codecs keep their standard stereo Audio InfoFrame path. Transport
  failures stop configuration; disconnected/incompatible sinks are rejected.
- The connected stereo sink may omit an explicit speaker-allocation block.
  Its actual stereo LPCM rate/sample-depth descriptor is required instead.
- Native EDID decoding validates full-block and DisplayID checksums, bounds and
  timing geometry. It handles detailed timings, DMT standard/established modes,
  CTA VICs (including extended VICs), YCbCr420-only restrictions, DisplayID type I
  / VII timings, HDMI/SCDC and stereo audio capabilities. Imported numeric
  DMT/CTA tables retain the original MIT notice and are shipped with that notice.
- RGB8 mode selection respects the connector's real pixel/TMDS limits and SCDC
  availability. It returns a candidate, never claims that the mode is active.
- UEFI EDID capture binds to the exact GOP output handle, avoiding another
  monitor's EDID on multi-GPU systems. EDID bytes and boot metadata are retained
  after boot-services memory becomes reclaimable.
- The GOP-associated PCI device's AMD ROM is copied through UEFI PCI I/O as data,
  without enabling a ROM BAR or executing option-ROM machine code. Its pages
  remain reserved. A bounded external-driver ATOM parser validates PCI/ROM/table
  extents, chain identity and legacy checksum. The SetPixelClock v1.7 parameter
  builder uses the required 100-Hz units; it does not execute the command yet.

Read-only Windows inspection identified RX6600 (`1002:73ff`), AMD HDA
`1002:aa01`, and Acer VG270 W3. The actual monitor EDID offers 1920x1080 at
239.998 Hz, using 558.100 MHz pixel clock with a 600 MHz TMDS limit and SCDC.
**NexisOS has not switched this physical monitor to that mode.** Its EDID was
only decoded as a local fixture; the monitor's serial data is not published.

## Remaining implementation

1. Replace the init-only external graphics ABI with retained code/state and
   checked services for hardware access, firmware commands and driver callbacks.
2. Implement the AMD ATOM interpreter/native display integration, DCN302 scanout,
   display-clock management, connector discovery and DDC/SCDC/link setup for
   RX6600. Unsupported firmware command versions must fail explicitly.
3. Implement the other four actual physical card backends. PCI recognition,
   GOP output and a successful download are not evidence of native support.
4. Integrate active-mode readback/vblank cadence and HDMI packet/clock programming
   with HDA output discovery, including sink removal/reconnection.
5. Publish each implemented, tested card module, pin its hash, select only the
   matching device and persist that payload during installation.
6. Verify native refresh and HDMI PCM on physical hardware. QEMU boot/audio
   checks cannot verify RX6600 display clocks, SCDC or HDMI transmission.

## Evidence

The latest reports are `build/hdmi-tests/report.json`,
`build/hdmi-tests/qemu-hda-report.json`, `build/edid-tests/report.json`,
`build/gpu-firmware-tests/report.json` and `build/boot-tests/report.json`.
`build/PHYSICAL_DISPLAY_MANIFEST.json` ties shipped artifacts to these sources
and reports. Model tests verify commands/parsing only. QEMU checks verify generic
HDA DMA, boot self-tests, desktop startup, DHCP and unchanged disposable disks.

References: [AMD HDA descriptors](https://raw.githubusercontent.com/torvalds/linux/v6.12/sound/pci/hda/hda_eld.c),
[AMD codec commands](https://raw.githubusercontent.com/torvalds/linux/v6.12/sound/pci/hda/patch_hdmi.c),
[AMD firmware tables](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/amd/include/atomfirmware.h),
[AMD display architecture](https://docs.kernel.org/gpu/amdgpu/display/index.html),
[UEFI PCI I/O](https://raw.githubusercontent.com/tianocore/edk2/master/MdePkg/Include/Protocol/PciIo.h).
