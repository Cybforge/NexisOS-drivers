# NexisOS display drivers

Signed, device-matched display driver packages for [NexisOS](https://github.com/Cybforge) (a from-scratch UEFI
operating system written in C). The OS image contains **no** driver binaries: it detects the primary display
adapter at run time and downloads the one matching package from this repository in the background. There is no
Store entry and nothing to click.

> **Honest status:** the download / signature / cache / install / safety-net machinery is tested end to end in QEMU.
> The AMD Radeon RX 6600 driver is complete as source and is verified against **register models only**
> (host tests, sanitizers, fault injection). **No driver in this repository has run on physical GPU hardware yet.**
> That is why every hardware package is a *trial* package (see "Safety nets").

## Packages

| Package | Hardware | Status |
| --- | --- | --- |
| `bochs` (v2) | QEMU standard VGA / Bochs DISPI, PCI `1234:1111` | implemented, tested in QEMU (virtual test device) |
| `rx6600` (v1) | AMD Radeon RX 6600 / 6650 XT, Navi23 (DCN 3.0.2), PCI `1002:73ff`, `1002:73ef` | complete native HDMI sequence incl. full refresh rate and HDMI audio; register-model verified only, **not run on hardware** |
| AMD RDNA2 big (RX 6700 / 6800 / 6900) | Navi21 / Navi22 | **not implemented**, no package published |
| AMD Polaris / Vega (RX 580 ...) | GCN4/5, DCE 11/12 / DCN 1 | **not implemented**, no package published |
| AMD RDNA1 (RX 5700 ...) | Navi10, DCN 2.0 | **not implemented**, no package published |
| Intel UHD / Iris Xe | Gen9-Gen12 display engine | **not implemented**, no package published |

On an adapter without a package nothing is downloaded or installed and the firmware (UEFI GOP) framebuffer simply
stays in use.

## What the RX6600 driver does

Linux' `amdgpu` display core (v6.12, `link_dpms.c`, `dcn20_hwseq.c`) is the reference for the order of operations.
Every step is a separately tested register transaction with a rollback:

1. raise the SMU hard-minimum clocks, prepare the DFS/DTO clock request, run AMD's DML bandwidth / watermark math
2. stop the OTG and the old transmitter (the screen goes dark here)
3. program display/DPP clocks, HUBP fetch + HUBBUB watermarks, DPP color path, OTG timing and global sync
4. ATOM firmware `SetPixelClock` (the board's own VBIOS tables), PHY PLL pixel resync
5. SCDC over DDC (scrambling and the 1:40 clock ratio above 340 MHz), HDMI / AFMT stream setup
6. AZALIA audio endpoint from the monitor's EDID (CTA-861 audio block) and the audio wall-clock DTO
7. UNIPHY transmitter on, OTG on, measure the real pixel clock with the frame counter, check scrambler / sink lock
8. HUBP visible, AVMUTE off, audio packets and endpoint on

Any failing step undoes the completed ones in reverse and "relights" the previous mode. Before the first change to the
target mode the sequence is run once for the *current* mode as a self-test, so a broken sequence on a board costs a
second of black screen, not the target mode. Audio problems degrade to video-only; they never cost the picture.
All steps log through the kernel log (`[GPU:rx6600] ...`).

The target is chosen from the monitor's EDID: the highest refresh rate it advertises at the desktop's current
resolution that the link can carry (for example 1920x1080 at 239.998 Hz over HDMI, which needs scrambling). Modes
outside 25 MHz .. the sink's maximum TMDS clock are rejected before any register is written. The resolution stays
the one UEFI chose, so the framebuffer the desktop draws into does not move.

## How a driver reaches the machine

1. The kernel reads the PCI IDs of the adapter UEFI/GOP is using and looks them up in a compiled-in table
   (`kernel/drivers/gpu/pins.h`, generated from `catalog.json`).
2. **Installed system:** a verified copy in `/opt/nexis-drivers/<name>.ndpk` is loaded immediately; the download then
   only checks for a newer version (used from the next boot).
   **Live medium or first boot after installation:** `<name>.ndpk` is downloaded in the background from
   `https://raw.githubusercontent.com/Cybforge/NexisOS-drivers/main/packages/`, verified, cached and started at once.
   A live session keeps `/opt` in RAM, so it downloads again on every boot; the installer copies the verified
   package to the installed system's persistent `/opt`.
3. The module is mapped, started and attached to the display compositor; the desktop switches to the native mode
   without a restart, and the HDMI audio endpoint is handed to the HD-Audio driver.

## Package format and trust

A package is an NDPK envelope: `"NDPK"`, format, module size, version, name, then the position-independent module
(NDRV v2: no imports, no runtime relocations, built with Zig; see `scripts/build_gpu_module_v2.py`), followed by a
64-byte **ECDSA P-256 / SHA-256** signature. The public key is compiled into the kernel (BearSSL verification); the
private key never leaves the maintainer's machine and is not in this repository. The catalog also carries a minimum
version per package, so an old, validly signed package cannot be replayed. A tampered or unsigned file is rejected and
the firmware framebuffer stays in use. Modules run supervisor-mode code: the trust anchor is the signature, not the
GitHub URL.

## Safety nets (hardware drivers are trial packages)

* **Start-up countdown:** 10 s after the package is verified, with a notice. **Esc** skips the driver for this boot
  (the package stays cached). Without it a driver that hangs the machine on start-up would hang every live-medium boot.
* **Trial prompt:** after the native mode is active, **Enter** keeps it; **Esc** or no input within 15 s restores the
  firmware mode. A black screen cannot become permanent.
* **Crash marker:** `/opt/nexis-drivers/<name>.try` is written before the module starts and removed when it ended
  cleanly. If the machine hangs inside the driver, that version is skipped on the next boot (installed systems).
* **Kernel options:** `nogpudriver` (never load a driver), `gpuautokeep` (no countdown, no trial prompt),
  `gpudriverlocal` (download from `http://10.0.2.2:8930/`, for QEMU tests).

## Repository layout

* `packages/<name>.ndpk` - the signed packages (what the kernel downloads)
* `catalog.json` - what each package serves and its status
* `tools/gpu-driver/` - driver sources: `bochs/`, `amd/` (RX6600 / DCN 3.0.2 components, ATOM interpreter, DML port),
  `common/` (logging, mode-switch sequencer, CTA-861 audio parser), `include/nexis_gpu_v2.h` (module ABI)
* `kernel/drivers/gpu/` - the OS side: catalog, package verifier, retained-module runtime, download / cache / trial logic
* `scripts/` - package build, signing and publishing (`build_gpu_catalog.py`, `gpu_package.py`,
  `publish_gpu_drivers.py`), register generators (`generate_dcn302_*.py`, from pinned Linux v6.12 sources) and tests
* `tests/host/` - register-model tests with fault injection
* `docs/GPU_DRIVERS.md` - design notes (German)

## Tests

| Test | What it proves | What it does not prove |
| --- | --- | --- |
| `scripts/test_rx6600_modeset.py` | full mode switch, rollback, relight and audio against a register model with fault injection (1,849 scenarios) | that real silicon behaves like the model |
| `scripts/test_rx6600.py`, `test_dcn302*.py` | each hardware transaction, retained module at two PIC addresses, UBSan | physical behavior |
| `scripts/test_modeset_seq.py`, `test_cta_audio.py` | sequencer, EDID audio-block parser (200k fuzz cases) | - |
| `scripts/test_gpu_platform_qemu.py` | download, signature check, tampered package rejected, unmatched adapter downloads nothing | - |
| `scripts/test_gpu_install_qemu.py` | installer persists the package, two reboots without network | - |
| `scripts/test_gpu_countdown_qemu.py` | countdown, Esc skip, automatic revert (needs a trial-marked test image) | - |

## Reporting hardware results

If you run a hardware package, the kernel log (`dmesg`) shows every step. A failed run that restores the firmware mode,
or a log that stops at a specific step, is exactly the information needed to fix the next iteration. The remaining
hardware families are only worth publishing once someone with that hardware can test them.

License: MIT. No third-party GPU firmware or Linux driver binaries are included; AMD's DML math sources keep their
original MIT notices.
