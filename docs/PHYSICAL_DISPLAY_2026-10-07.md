# Physical display work: 2026-10-07

Build ID: `physical-display-20261007-r4` (intermediate resource-service build).

The requested end state remains five physical GPU display drivers including
RX6600, the highest supported monitor refresh rate, HDMI sound, and external
device-matched download/persistent installation. **This end state is not yet
implemented or verified.** Bochs/QEMU support does not count as a physical driver.

## Latest external native code after r4

The latest RX6600 candidate adds native DAL SMU mailbox initialization,
protocol/version checks and actual supported SOCCLK/UCLK/DCEFCLK/DISPCLK/DPPCLK/PHYCLK
levels. It can send native acknowledged hard-minimum clock requests; these
acknowledgments are not measured scanout frequencies. Pending/uncertain requests
are quarantined instead of reset or blindly replayed. Probe makes firmware
queries but does not change clock/power or display policy.

The native DCN302 DFS/DTO transaction now reads the actual CLK02 PLL feedback
multiplier and completed display/DPP divider states, including per-pipe DTOs.
It quantizes clocks without undershooting the request, validates real owned
SMU floors for both the old/new clock, requires all five OTG/VTG pipelines
stopped and applies the native clock registers. Exact fixed-point PLL math
avoids insufficient MHz voltage floors at fractional clock boundaries. A
rollback is allowed only after stable completed native readback; unresolved
requests or failed rollback leave the transaction poisoned and pipelines off.
Display divider 127 transitions and buffered DTO configurations remain explicit
unsupported states until their native FIFO/buffering handling is implemented.

Both operations are integrated into the retained RX6600 module code. Clock
floors cannot be lowered below its programmed clocks or outstanding rollback
requirements. The PIC tests retain two allocations simultaneously and verify
distinct actual addresses, then execute native SMU floors and display-clock
apply/restore after module entry returns. These are development operations,
not Store commands. The normal set_mode still rejects a changed video mode.

The retained RX6600 code now reads GDDR6 geometry/capacity/channel masks from
VRAM_INFO 2.3/2.4/2.5. All advertised modules must agree; truncated modules,
unbounded shifts, unsupported formats and ambiguous geometry are rejected.
No fixed assumed RX6600 memory width or firmware default clock is used.

The complete AMD DCN30 VBA and RQ/DLG math engine is imported reproducibly
from checksum-pinned Linux v6.12 MIT sources, retaining notices and algorithm.
Platform adaptations replace only includes/logging/assertion transport and two
undefined type-punning expressions with defined bit copies. The DCN3.02 ASIC
template omits upstream placeholder clocks/states, min DCFCLK and VCO.

The single linear RGB8 adapter rejects unbounded inputs and overlapping job/
workspace memory, performs mode-support validation, and calculates required
DISP/DPP clocks, watermarks, VSTARTUP/update/ready and RQ/DLG/TTU fields. RQ/DLG
uses the supplied effective clocks, never silently treats the calculated
minimum as a programmed DFS setting. The retained callback selects current
versus explicitly prepared clocks and uses board geometry, owned acknowledged
UCLK/DCF/SOC/PHY floors, GDDR6 UCLK*16 MT/s and the actual PLL multiplier. AMD's
DCN302 bounding-box fabric frequency follows DCFCLK; it is not a measured FCLK.
The normal framebuffer dimensions remain required. Math makes no MMIO writes.

An assembly entry saves x87/XMM0..15/MXCSR before any C prologue, uses masked
nearest-even FP mode and restores exact state on successful or rejected math.
Nested/reentrant computations are rejected. A native SysV jump context replaces
Clang's Windows-SEH-offset-dependent builtin setjmp: a failed assertion returns
through the correct frame rather than crashing the sanitizer host process.

The retained RX6600 now also contains real native HUBP RQ/DLG/TTU programming:
42 exact Navi23 register addresses and 70 field mappings are generated from
the inherited AMD DCN3/DCN2/DCN2.1 routines. Field overflows are rejected before
writes. The actual DCN3 blank inheritance is checked: NO_OUTSTANDING_REQ is
drained before blank, bounded to 100ms, and TTU_DISABLE is zero, unlike DCN1.
Application requires all five OTGs/VTGs stopped and a powered, clocked, blanked
and drained HUBP. Status/W1C bits are excluded from writes; unrelated RW bits
are preserved. Posted write failures are tracked before issuance and restored
in reverse order only after proving the native stopped state again. A failed
restoration leaves the transaction poisoned and the parent must keep it off.
The retained callback binds the successful DML plan to its actual current or
prepared native clocks and owned UCLK/DCF/SOC/PHY floors. Stale clocks/floors
reject application; dirty HUBP plans prevent lowering their required floors.
This executes after module entry at two simultaneously allocated PIC bases.
Readback verifies configuration/shadow fields, not active physical scanout.

The retained RX6600 also programs native HUBBUB watermarks and fixed-floor
policy. The actual AMD DCN3 inheritance obtains the ROM crystal through DCCG
and then applies HUBBUB's enabled timer/divider. DML previously used the
undivided crystal directly; it now uses the verified 40..60-MHz native HUBBUB
reference. Unknown alternative sources, disabled timers and unstable reference
controls are rejected. No assumed RX6600 clock is substituted.

The checked DML urgent-bandwidth fractions use 1000-unit fixed point. Nanosecond
watermarks convert with 64-bit arithmetic, rounding up, and must fit Navi23's
actual 14/16-bit fields; the larger clamp in shared upstream code is unsuitable
for these native fields. The same minimum-floor plan populates all four sets.
The model permits neither self refresh nor memory clock change, so SR/PSTATE
allow signals are forced low before the watermark writes. Old HUBP fetch
registers are restored first, then old watermarks and old force policy last.
Read/write faults, posted and ignored writes and lost pipeline identity are
exercised in the real retained PIC code. Failed restoration keeps it quarantined
with scanout off. UCLK changes and conflicting DFS operations are rejected
while the fixed-floor plan is installed. Floor changes also revalidate PCI
resources before any mailbox write. These are native configuration operations;
they do not prove that a physical firmware memory transition has completed.

Native stopped timing/global-sync is now retained with the same successful DML
result as HUBP/HUBBUB: VSTARTUP, VUPDATE, VREADY, full progressive geometry,
polarity, VTG initialization and fixed-rate controls. The new transaction uses
DCN3's actual GLOBAL_CONTROL2 lock selector, bounded lock acknowledgment and
pending drain, and disables timing double buffering while programming. It
preserves unrelated configuration and never replays status/instant-trigger bits.
Before and after every write it revalidates PCI extents, exact native DFS clocks,
all six owned SMU floors, installed HUBP fetch state and HUBBUB policy/reference,
and stopped/powered OTGs/VTGs. Restore puts the old timing back before releasing
fetch/watermarks/policy or clocks. Posted/ignored writes and lost pipeline,
clock, reference, resource or fetch state are checked in the actual retained
PIC code; poisoned rollback retains dependencies until explicit restoration.
Successful restoration does not silently restore parent readiness.

Current checks: SMU 197 cases, DFS/DTO 1,410 cases, retained RX6600 1,105 cases,
ATOM memory 2,074 cases, complete DML/PIC math 467 cases, HUBP 6,701 cases and
HUBBUB 7,972 cases, plus native timing/global-sync 4,854 cases;
all pass with warnings treated as errors and the undefined-behavior sanitizer.
The RX6600 candidate is 217,152 bytes with 442,368-byte retained memory, no imports
or runtime relocations. The MMIO/firmware responses are modeled, not physical
hardware evidence. The 1080p 240-Hz timing passes the math test, which does not
mean NexisOS has physically switched the card to that mode. The pure math
harness is never installed as a driver. Full modeset composition,
scaler/cursor proof, pixel PLL/PHY, complete link rollback
and AZALIA/HDA audio coordination remain required, along with full native
memory/clock-policy transition proof and the other four card
backends. The candidate stays outside the catalog and is not embedded in the
unchanged r4 ISOs. No new physical-driver ISO has been delivered by this increment.

## Completed work in this build

- Retained ABI2 now exposes independently verified, read-only PCI resource
  metadata, including large VRAM apertures. UEFI sizes are checked against live
  PCI identity/class/type/command and both BAR samples. 64-bit upper slots are
  excluded. Alignment, 32/64-bit limits and overlapping apertures are checked.
  Large VRAM is not mapped as registers. Packed boot fields are copied into
  aligned arrays. Legacy 104-byte and resource 112-byte service prefixes remain
  explicit in the module header; the old prefix is tested against a guard page.
- The external retained RX6600 adapter now joins the actual ATOM board wiring,
  native DIG/FE/OTG/OPP route, HUBP/GOP VRAM translation and measured frame clock.
  read_mode/poll/shutdown run after entry returns. Changed routing, surfaces,
  PCI resources, hotplug and abnormal frame progression invalidate the output.
  At the r4 checkpoint, 192 host cases executed normal and loaded PIC callbacks, also
  with UB sanitizer. This is a **development candidate, not a completed or
  distributed driver**: set_mode only verifies an unchanged running mode.
  Clock-changing modesetting, memory bandwidth, PHY and audio remain required.
  The candidate is not embedded in either ISO or added to the Store catalog.
  Later VRR/min/max-total changes invalidate a previously measured fixed clock.

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

The new revision adds a bounded external ATOM bytecode interpreter. It checks
the reachable command tables, instruction boundaries, operands and nested
parameter windows before touching hardware. Register/PLL/MC accesses use
checked callbacks. Indirect I/O, arithmetic, masks, conditions, command calls,
scratch access and delays are implemented. Unsupported repeat/save/restore,
PCI/SYSIO and unknown operations fail explicitly. Recursion, instructions,
wall time and delays are bounded. IO failure stops immediately; a display
backend must still implement hardware rollback. This interpreter is external
supporting source and is not yet a complete RX6600 display backend.

Retained module ABI2 and its kernel loader are implemented. PIC code, state and
callbacks remain mapped; code and data have separate permissions, including
RAM identity aliases. The shared module page-table branch is reserved before
process creation. Firmware PCI resource extents are handed over without writing
BARs. MMIO services check the BAR identity and bounds; the current implementation
accepts register windows up to 16 MiB below 64 GiB. Activation requires a valid
native hardware scanout readback with the existing framebuffer geometry. The
compositor follows this read-back refresh rate and retains 60 Hz fallback.
**No physical card module is in the catalog yet**, so these services do not
switch an RX6600 out of GOP or establish HDMI transmission on their own.

Read-only Windows inspection identified RX6600 (`1002:73ff`), AMD HDA
`1002:aa01`, and Acer VG270 W3. The actual monitor EDID offers 1920x1080 at
239.998 Hz, using 558.100 MHz pixel clock with a 600 MHz TMDS limit and SCDC.
**NexisOS has not switched this physical monitor to that mode.** Its EDID was
only decoded as a local fixture; the monitor's serial data is not published.

## Remaining implementation

External backend source added after the r2 image (not loaded by that image):

- DCN302/Navi23 OTG hardware register programming, native scanout enable/disable,
  current geometry readback, hardware update locking and rollback. Single RGB
  progressive pipelines are validated without rewiring the firmware's OPP.
- Native shared I2C controller transactions for DDC1..DDC5, 144-byte FIFO,
  repeated starts, SCDC/EDID reads, ownership, clock setup, explicit DONE,
  bounded NACK/timeout/aborted handling and readback. A failed restoration
  quarantines the context. Firmware preemption releases only our request,
  without resetting another owner's transfer. AUX/software-routed pads are
  rejected; a future connector adapter must establish the correct I2C routing.
- SCDC monitor configuration at address 0x54, source/sink version, scrambling
  and 1:40 clock ratio above 340 MHz, exact configuration readback, monitor
  channel/clock-lock verification and restoration of prior settings.
- Native HDMI RGB8 stream and AFMT stereo audio/ACR programming. Audio rates
  are mathematically consistent with the supplied actual clock. Update strobes
  are handled separately from stable fields; failed preparation rolls back.
  Commit requires the native transmitter clock and enable state and controls
  audio samples/AVMUTE. The parent must supply a verified AZALIA endpoint and
  perform the actual PLL/PHY programming and HDA coordination.
- Native fixed-rate frame-counter frequency measurement against a calibrated
  clock, with wrap handling, bounds, timing stability and observation uncertainty.
  Its result never uses an advertised or requested pixel clock. It runs at
  takeover/modeset rather than blocking the compositor's ordinary poll.
- A freestanding PIC link check retains the real native code bodies, forbids
  imports/runtime relocations and includes local memory primitives. Its test
  fixture rejects activation and is not a physical card module or download.

Model checks passed: OTG 725 cases, native I2C plus SCDC 1,360 cases, HDMI/AFMT
862 cases, frame-counter clock proof 78 cases, board/command decoding 5,242
cases, native route binding 3,227 cases and native surface proof 330 cases,
all also under the undefined-behavior sanitizer. These 11,824 cases check code and modeled register/protocol
semantics; **they do not verify RX6600 hardware, an active 240 Hz mode, or HDMI
sound**. The full five-card end state remains open. No new physical card binary
or pin/catalog entry has been created from this partial backend.

### Native board and active-pipeline components added after r2

- `atom_board.c` decodes bounded DisplayObjectInfo 1.4/1.5, GPIO 2.1 and DCE
  4.1..4.5 table layouts. GPIO register indices retain all 32 bits. It rejects
  malformed record extents, duplicate/missing pin identities, invalid shifts,
  table versions and ambiguous connector identities; output remains empty
  after failure. Software I2C/external encoders remain explicit, not guessed
  into supported native paths. The actual RX6600 board ROM has not been run
  through these parsers on hardware yet.
- `atom_display_commands.c` builds board-derived COMBOPHY pixel clock 1.7
  (100-Hz units), stream setup 1.5 (correct RGB8 enum), and transmitter 1.6/1.7
  (32/60-byte parameter spaces). The command adapter uses the bounded VM and
  checks the firmware revision. Caller byte-buffer alignment is not assumed.
  Version1.7 supports legacy HDMI TMDS parameters here, not FRL/HPO startup.
- `dcn302_route.c` verifies a single active native RGB8 HDMI link, independently
  numbered FE/link/OTG/OPP/DDC/HPD and exact board GPIO/register correspondence.
  It rejects multi-monitor ambiguity, split ODM, external converters, missing
  HPD, unknown routing and observed state changes. Clock remains zero until
  measured from actual OTG counters; no GOP/EDID clock is substituted.
- `dcn302_surface.c` follows native MPC -> paired DPP/HUBP, validates linear
  uncompressed VM0 RGB8888, viewport/pitch/crossbar and actual INUSE address,
  and independently translates the physical GPU VRAM offset into the verified
  CPU PCI aperture. It rejects pending-address disagreement, tiling, DCC/TMZ,
  wrong formats/planes, underflow and bounds/state changes. Its caller still
  needs the independently verified VRAM BAR resource from the kernel.
- HDMI packet setup now takes separate stream and physical-link indices. The
  earlier same-number assumption was incorrect for real independently routed
  pipelines. Both preparation and commit validate link/FE routing and clocks;
  configuration errors before writes and changes before unmute are rejected.
  Fault injection checks rollback for mixed stream/link numbers too.

All these additions are external native support source and have a no-import,
no-relocation PIC link check. They are **not** a complete loadable RX6600 module,
not catalog entries and not contained in the unchanged r2 OS images. Full
display/memory clock and bandwidth management, actual PHY modeset rollback,
AZALIA endpoint/HDA coordination, retained module integration, four other card
backends, catalog/persistence and physical verification remain required.

### r3 kernel readback correction

`physical-display-20261007-r3` includes a corrected retained-mode validation
policy: native clocks explicitly measured against calibrated time can differ
by at most 1,000 ppm from nominal requested clocks. Geometry and polarity still
match exactly; unsupported flags/reserved fields, invalid clocks and fake
non-measured clock differences are rejected. The compositor receives the
actual read-back timing, not the nominal requested clock. Valid zero-back-porch
geometry is no longer rejected merely because sync ends at total.
The actual policy passed 721 host/UBSan cases. This build still has zero complete
physical GPU module entries; it is an intermediate correction, not the full
five-card/max-Hz/HDMI delivery. External components above are not embedded.

1. Connect actual physical card modules to the retained ABI2 loader and catalog.
2. Implement AMD native display integration using the bounded ATOM executor, DCN302 scanout,
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
`build/gpu-runtime-tests/report.json` checks the real PIC package/image decoder,
independent writable state after init, retained callbacks and memory permissions
under Windows. It does not exercise kernel MMIO or physical GPU modesetting.
ATOM tests include 3,000 bytecode mutations and an undefined-behavior sanitizer
run. Loader tests and virtual audio do not prove physical display functionality.

References: [AMD HDA descriptors](https://raw.githubusercontent.com/torvalds/linux/v6.12/sound/pci/hda/hda_eld.c),
[AMD codec commands](https://raw.githubusercontent.com/torvalds/linux/v6.12/sound/pci/hda/patch_hdmi.c),
[AMD firmware tables](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/amd/include/atomfirmware.h),
[AMD display architecture](https://docs.kernel.org/gpu/amdgpu/display/index.html),
[UEFI PCI I/O](https://raw.githubusercontent.com/tianocore/edk2/master/MdePkg/Include/Protocol/PciIo.h).
