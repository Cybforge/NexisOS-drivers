#!/usr/bin/env python3
"""Compile and run the actual bounded AMD firmware parser / UEFI ROM adapter."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--sanitizer',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'build/gpu-firmware-tests';out.mkdir(exist_ok=True)
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
reports={}
for name,sources in {
    'atom':['tests/host/test_atom_tables.c','tools/gpu-driver/amd/atom_tables.c'],
    'atom_vm':['tests/host/test_atom_vm.c','tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_vm.c'],
    'uefi_rom':['tests/host/test_efi_gpu_rom.c'],
}.items():
    exe=out/(name+'.exe')
    subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(root/p) for p in sources],'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    if result.returncode:raise SystemExit(result.stderr or result.stdout)
    reports[name]=json.loads(result.stdout)
if args.sanitizer:
    zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
    exe=out/'atom_vm_ubsan.exe'
    subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=all',
                    *[str(root/p) for p in ['tests/host/test_atom_vm.c','tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_vm.c']],'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
    assert json.loads(result.stdout)==reports['atom_vm']
    reports['atom_vm']['undefined_behavior_sanitizer_passed']=True
reports['source_sha256']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_tables.h','tools/gpu-driver/amd/atom_vm.c','tools/gpu-driver/amd/atom_vm.h','tests/host/test_atom_vm.c','boot/efi/gpu_rom.h']}
reports['native_modesetting_implemented']=False
(out/'report.json').write_text(json.dumps(reports,indent=2)+'\n',encoding='utf-8');print(json.dumps(reports,indent=2))
