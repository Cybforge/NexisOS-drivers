#!/usr/bin/env python3
"""Host test of the RX6600 mode-switch orchestrator against an extended register model (modeled only)."""
from pathlib import Path
import argparse, json, os, shutil, subprocess
ap = argparse.ArgumentParser()
ap.add_argument('--sanitizer', action='store_true')
args = ap.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'rx6600-modeset-tests'
out.mkdir(parents=True, exist_ok=True)
amd = 'tools/gpu-driver/amd/'
sources = ['tests/host/test_rx6600_modeset.c', 'tools/gpu-driver/amd/rx6600.c', 'tools/gpu-driver/amd/rx6600_modeset.c',
           'tools/gpu-driver/common/nxlog.c', 'tools/gpu-driver/common/modeset_seq.c', 'tools/gpu-driver/common/cta_audio.c', 'kernel/drivers/gpu/mode_policy.c'] + [amd + n for n in [
    'atom_tables.c', 'atom_board.c', 'dcn302_route.c', 'atom_vm.c', 'atom_display_commands.c', 'dcn302_surface.c', 'dcn302_otg.c',
    'dcn302_clock.c', 'dcn302_smu.c', 'dcn302_dfs.c', 'atom_memory.c', 'dcn302_dml.c', 'dcn302_hubp.c', 'dcn302_hubbub.c',
    'dcn302_timing.c', 'dcn302_dpp.c', 'dcn302_ddc.c', 'dcn302_audio.c', 'dcn302_hdmi.c', 'hdmi_scdc.c', 'dcn302_pixel_resync.c',
    'dml/nexis_dml_port.c', 'dml/display_mode_vba.c', 'dml/display_mode_vba_30.c', 'dml/display_rq_dlg_calc_30.c',
    'dml/dcn_calc_math.c', 'dml/display_rq_dlg_helpers.c']]
gcc = os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or 'C:/Tools/w64devkit/bin/gcc.exe'
exe = out / 'rx6600_modeset.exe'
warnings = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-pointer-sign']
subprocess.run([gcc, '-std=c11', '-O2', *warnings, *[str(root / s) for s in sources], '-o', str(exe)], check=True)
r = subprocess.run([str(exe)], capture_output=True, text=True)
if r.returncode:
    raise SystemExit(r.stderr or r.stdout or 'modeset test failed with exit code %d' % r.returncode)
report = json.loads(r.stdout)
if args.sanitizer:
    zig = os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home() / 'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
    exe2 = out / 'rx6600_modeset_ubsan.exe'
    subprocess.run([zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-O1', *warnings, '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    *[str(root / s) for s in sources], '-o', str(exe2)], check=True)
    r2 = subprocess.run([str(exe2)], capture_output=True, text=True, check=True)
    assert json.loads(r2.stdout) == report
    report['undefined_behavior_sanitizer_passed'] = True
(out / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
