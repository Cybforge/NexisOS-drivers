#!/usr/bin/env python3
"""EDID audio-capability parser: valid/damaged blocks and a 200k-case mutation fuzz under the UB sanitizer."""
from pathlib import Path
import json, os, shutil, subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'cta-audio-tests'
out.mkdir(parents=True, exist_ok=True)
zig = os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home() / 'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
exe = out / 'test_cta_audio.exe'
subprocess.run([zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                '-fno-sanitize-recover=all', str(root / 'tests/host/test_cta_audio.c'), str(root / 'tools/gpu-driver/common/cta_audio.c'),
                '-o', str(exe)], check=True)
r = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
report = json.loads(r.stdout)
report['undefined_behavior_sanitizer_passed'] = True
(out / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
