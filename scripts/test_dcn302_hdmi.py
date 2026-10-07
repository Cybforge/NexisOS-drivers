#!/usr/bin/env python3
"""Check actual native DCN302 HDMI/AFMT code, strobes, audio rate and rollback."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/dcn302-hdmi-tests';out.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');args=p.parse_args()
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
sources=['tests/host/test_dcn302_hdmi.c','tools/gpu-driver/amd/dcn302_hdmi.c'];exe=out/'hdmi.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True)
if result.returncode:raise SystemExit(result.stderr or result.stdout)
report=json.loads(result.stdout)
if args.sanitizer:
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe');exe=out/'hdmi_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert json.loads(result.stdout)==report
 report['undefined_behavior_sanitizer_passed']=True
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+['tools/gpu-driver/amd/dcn302_hdmi.h','tools/gpu-driver/amd/dcn302_hdmi_regs.h','scripts/generate_dcn302_hdmi_regs.py','scripts/generate_dcn302_regs.py']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
