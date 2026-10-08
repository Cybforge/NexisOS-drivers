#!/usr/bin/env python3
"""Host test of the generic modeset apply/undo/relight runner (pure logic, no hardware model)."""
from pathlib import Path
import json, os, shutil, subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'modeset-seq-tests'
out.mkdir(parents=True, exist_ok=True)
gcc = os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
exe = out / 'test_modeset_seq.exe'
subprocess.run([gcc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(root / 'tests/host/test_modeset_seq.c'),
                str(root / 'tools/gpu-driver/common/modeset_seq.c'), '-o', str(exe)], check=True)
r = subprocess.run([str(exe)], capture_output=True, text=True)
if r.returncode:
    raise SystemExit(r.stderr or 'test failed')
report = json.loads(r.stdout)
(out / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
