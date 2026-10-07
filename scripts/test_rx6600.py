#!/usr/bin/env python3
"""Exercise the actual retained RX6600 backend; never install its pending binary."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
from build_gpu_module_v2 import build
root=Path(__file__).resolve().parents[1];out=root/'build/rx6600-tests';out.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');args=p.parse_args()
native=['tools/gpu-driver/amd/rx6600_module.c','tools/gpu-driver/amd/rx6600.c',
 'tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_board.c','tools/gpu-driver/amd/dcn302_route.c',
 'tools/gpu-driver/amd/dcn302_surface.c','tools/gpu-driver/amd/dcn302_otg.c','tools/gpu-driver/amd/dcn302_clock.c']
artifact=out/'candidate.ndrv';metadata=build([root/s for s in native+['tools/gpu-driver/common/memory.c']],artifact,0x1002,0x73ff)
sources=['tests/host/test_rx6600.c','kernel/drivers/gpu/module_image.c']+native
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe';exe=out/'rx6600.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(artifact)],capture_output=True,text=True)
if r.returncode:raise SystemExit(r.stderr or r.stdout)
report=json.loads(r.stdout)
if args.sanitizer:
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe');exe=out/'rx6600_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
 r=subprocess.run([str(exe),str(artifact)],capture_output=True,text=True,check=True);assert json.loads(r.stdout)==report
 report['undefined_behavior_sanitizer_passed']=True
report.update(candidate=metadata,distributed=False,full_rx6600_driver_complete=False,physical_gpu_drivers_complete=0)
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+['tools/gpu-driver/amd/rx6600.h',
 'tools/gpu-driver/common/memory.c','tools/gpu-driver/include/nexis_gpu_v2.h','scripts/build_gpu_module_v2.py','scripts/test_rx6600.py']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
