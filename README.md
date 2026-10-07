# NexisOS external graphics drivers

This repository currently contains one implemented driver: **QEMU standard VGA / Bochs DISPI**, PCI `1234:1111`.
It does not fulfill the request for five physical graphics drivers.

Native physical-driver work is in progress. `kernel/drivers/audio/hdmi.c`
contains an AMD `1002:aa01` stereo HDA codec helper, and `tools/gpu-driver/amd/`
contains the initial bounded ATOM firmware parser. EDID/connector mode policy
and read-only UEFI ROM capture are also included as supporting source.
These pieces do not yet initialize RX6600 scanout, set its display/link clocks,
or establish HDMI transmission. See `docs/PHYSICAL_DISPLAY_2026-10-07.md`.
It is a native register driver for NexisOS, not a Linux .ko module. On a matching adapter it takes over the existing framebuffer dimensions, programs 32-bit DISPI scanout and reuses the compositor's RAM back buffer.

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
