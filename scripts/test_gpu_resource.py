#!/usr/bin/env python3
"""Exercise the kernel's actual read-only firmware/live PCI resource policy."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/gpu-resource-tests';out.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');args=p.parse_args()
sources=['tests/host/test_gpu_resource.c','kernel/drivers/gpu/pci_resource.c']
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe';exe=out/'resource.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True)
if r.returncode:raise SystemExit(r.stderr or r.stdout)
report=json.loads(r.stdout)
service_sources=['tests/host/test_gpu_resource_service.c','kernel/drivers/gpu/pci_resource.c','kernel/drivers/gpu/module_image.c','kernel/drivers/gpu/mode_policy.c']
exe=out/'service.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/s) for s in service_sources],'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True)
if r.returncode:raise SystemExit(r.stderr or r.stdout)
report['kernel_callback']=json.loads(r.stdout)
if args.sanitizer:
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe');exe=out/'resource_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert json.loads(r.stdout)=={k:v for k,v in report.items() if k!='kernel_callback'}
 exe=out/'service_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in service_sources],'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert json.loads(r.stdout)==report['kernel_callback']
 report['undefined_behavior_sanitizer_passed']=True
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+service_sources+['kernel/drivers/gpu/pci_resource.h','tools/gpu-driver/include/nexis_gpu_v2.h','kernel/drivers/gpu/runtime.c','scripts/test_gpu_resource.py']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
