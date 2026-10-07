# NexisOS external graphics drivers

This repository currently contains one implemented driver: **QEMU standard VGA / Bochs DISPI**, PCI `1234:1111`.
It does not fulfill the request for five physical graphics drivers.

Native physical-driver work is in progress. `kernel/drivers/audio/hdmi.c`
contains an AMD `1002:aa01` stereo HDA codec helper, and `tools/gpu-driver/amd/`
contains a bounded ATOM firmware parser and bytecode interpreter. EDID/connector mode policy
and read-only UEFI ROM capture are also included as supporting source.
The new DCN302 components implement native OTG timing/scanout register control,
hardware DDC/I2C transactions, SCDC scrambling/link verification, HDMI/AFMT
packet/audio clock setup and running frame-counter frequency measurement.
They now also decode ATOM board GPIO/DDC/HPD/UNIPHY wiring, build versioned
RGB8 HDMI pixel/stream/transmitter commands, bind independently numbered
stream and physical link encoders, and validate the actual linear HUBP VRAM
surface against the firmware PCI aperture and Desktop framebuffer.
They have register-model and freestanding PIC build checks. They are external
backend components. The new retained RX6600 adapter connects real ROM wiring,
native routing, VRAM surface proof and measured hardware timing, with read/poll/
shutdown callbacks. Its external PIC candidate executes in host register-model
checks at two verified distinct addresses. The native DAL SMU protocol now
queries supported clocks and sends acknowledged clock-floor requests. The
native DFS/DTO transaction reads the real PLL/dividers, quantizes clocks,
requires all five pipelines stopped and adequate owned SMU floors, and applies
and restores actual display/DPP clock registers. Uncertain pending requests
are not replayed; exact fractional PLL math protects voltage-floor rounding.
Full bandwidth/DLG, pixel PLL/PHY and coordinated HDMI audio modesetting remain
pending; it is not a complete card driver or catalog package. See
`docs/PHYSICAL_DISPLAY_2026-10-07.md`.
The existing Bochs module programs DISPI registers directly for NexisOS. On a matching virtual adapter it takes over the existing framebuffer dimensions, programs 32-bit DISPI scanout and reuses the compositor's RAM back buffer.

The retained ABI2 loader now provides persistent PIC code/state, checked
callbacks, bounded register services, firmware-reported PCI resource extents
and read-back mode validation. Its code pages are read-only/executable; data is
writable/NX, and RAM identity aliases cannot write executable code. ABI2 has
**no implemented physical card module in the download catalog yet**. The
loader test fixture is not a graphics driver and is never distributed as one.
The service resource extension independently checks UEFI memory extents against
live PCI BARs, including large VRAM, without mapping them or writing PCI config.
104-byte legacy and 112-byte resource service prefixes are explicitly versioned
in the module header; upper halves of 64-bit BARs cannot authorize another BAR.

## Actual support

| Function | Status |
| --- | --- |
| Native QEMU standard-VGA modesetting | Implemented; tested in QEMU |
| External module download selected by PCI ID | Implemented; tested |
| Pinned SHA-256 and ABI validation | Implemented; corrupted download rejected |
| Automatic background installation without a Store listing | Implemented |
| Live boot: RAM cache, download again next boot | Implemented |
| Installed system: verified module copied to persistent /opt | Implemented; two reboots with the download server stopped passed |
| Physical AMD RX6600 | **Not implemented** |
| Four other requested physical graphics cards | **Not implemented** |
| Highest physical monitor refresh rate / link training | **Not implemented** |
| GPU acceleration / hardware video decoding | **Not implemented** |
| HDMI audio in this graphics module | **Not implemented**; QEMU standard VGA has no audio device |

Do not install this driver on an AMD, Intel or NVIDIA physical card. NexisOS selects it only for the exact supported PCI device and firmware aperture. Unsupported cards retain their firmware framebuffer.

A physical RX6600 driver needs AMD-specific device/firmware initialization, display clocks, DCN scanout, EDID/DDC, HDMI/DisplayPort link configuration and coordination with an audio codec. These are not replaced by PCI identification or by this Bochs implementation. AMD's architecture is documented at https://docs.kernel.org/gpu/amdgpu/display/index.html .

## Layout and integration

- `tools/gpu-driver/bochs/bochs.c`: actual DISPI programming with rollback on failed attachment.
- `tools/gpu-driver/include/nexis_gpu.h`: versioned init-only ABI.
- `tools/gpu-driver/include/nexis_gpu_v2.h`: retained SysV x86-64 ABI.
- `scripts/build_gpu_module_v2.py`: retained external module builder, rejects imports and runtime relocations.
- `kernel/drivers/gpu/runtime.c`: retained module/scanout integration; activation requires hardware readback.
- `tools/gpu-driver/amd/atom_vm.c`: bounded board-bytecode execution with explicit unsupported-operation errors.
- `tools/gpu-driver/amd/dcn302_otg.c`: native Navi23 timing/VTG/OTG control; preserves the existing single OPP routing.
- `tools/gpu-driver/amd/dcn302_ddc.c`: actual hardware I2C FIFO/arbitration transactions; firmware-owned transfers are not reset.
- `tools/gpu-driver/amd/hdmi_scdc.c`: monitor scrambling/clock ratio configuration, readback, sink lock and rollback.
- `tools/gpu-driver/amd/dcn302_hdmi.c`: native RGB8 HDMI/AFMT packet and stereo PCM48 ACR setup, AVMUTE and rollback.
- `tools/gpu-driver/amd/dcn302_clock.c`: calibrated native frame-counter timing proof; no EDID/requested clock fallback.
- `tools/gpu-driver/amd/dcn302_smu.c`: actual native DAL mailbox, version/clock-limit checks, acknowledged clock floors and uncertain-request quarantine.
- `tools/gpu-driver/amd/dcn302_dfs.c`: native PLL/divider/DTO readback and stopped-pipeline display/DPP clock transaction with verified rollback and voltage-floor checks.
- `tools/gpu-driver/common/memory.c`: freestanding module memory primitives, without C library/kernel imports.
- `scripts/generate_dcn302*_regs.py`: exact register subsets generated from checksum-pinned AMD Linux v6.12 source definitions.
- `scripts/test_dcn302*.py`: native-code register models, transaction failures and undefined-behavior checks.
- `scripts/test_gpu_native_link.py`: real native code linked as an import/relocation-free NDRV2 fixture. The fixture always rejects activation and is never distributed.
- `tools/gpu-driver/module.ld`: image layout.
- `scripts/build_gpu_modules.py`: reproducible Zig 0.13 compiler build, rejects imports/relocations/writable program globals, creates the module and kernel pin.
- `kernel/drivers/gpu/`: NexisOS loader/cache integration. Call `gpu_packages_poll()` from the Store's idle job poll. The installer calls `gpu_prepare_install()` and saves `gpu_install_payload()` to `/opt/nexis-drivers/bochs.ndrv` in the installed root.
- `packages/bochs.ndrv`: **external binary, not embedded in the ISO**.
- `catalog.json`: truthful capabilities and pinned hash.
- `tests/host/test_gpu_module.c`: register model, input rejection and rollback.
- `tests/qemu-results.json`: actual takeover, corrupted-download and unmatched-device boot results.
- `tests/install-results.json`: disposable disk installation, exact ext4 payload/mode verification and two cached-driver reboots.

The OS checks an exact compiled-in hash before executing a module. A module is mapped supervisor-only, changed from writable/NX to read-only/executable, called once with interrupts disabled, then unmapped. Version 1 does not support persistent callbacks, GPU interrupts, DMA or writable module state. Loading a new module version requires updating the OS's pin and rebuilding the kernel; a mutable GitHub file cannot silently change kernel code.

Build the external module with `python scripts/build_gpu_modules.py` (set `NEXIS_ZIG` if Zig is not on PATH). The matching OS needs the framebuffer attach helpers in NexisOS and the Store/installer integration. Hardware register specification: https://www.qemu.org/docs/master/specs/standard-vga.html .

License: original source here is MIT licensed. No third-party GPU firmware or Linux driver binaries are included.
