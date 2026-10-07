#!/usr/bin/env python3
"""Exercise actual native Navi23 HUBP fields, posted failures and rollback."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/dcn302-hubp-tests';out.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');args=p.parse_args()
sources=['tests/host/test_dcn302_hubp.c','tools/gpu-driver/amd/dcn302_hubp.c']
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe';exe=out/'hubp.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True)
if r.returncode:raise SystemExit(r.stderr or r.stdout)
report=json.loads(r.stdout)
if args.sanitizer:
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe');exe=out/'hubp_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert json.loads(r.stdout)==report
 report['undefined_behavior_sanitizer_passed']=True
extra=['tools/gpu-driver/amd/dcn302_hubp.h','tools/gpu-driver/amd/dcn302_hubp_regs.h','tools/gpu-driver/amd/dcn302_dml.h','tools/gpu-driver/amd/dcn302_otg.h','tools/gpu-driver/amd/dcn302_regs.h','scripts/generate_dcn302_hubp_regs.py','scripts/test_dcn302_hubp.py']
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+extra}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
