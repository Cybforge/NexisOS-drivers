#!/usr/bin/env python3
"""Test the actual retained image decoder, PIC globals/callbacks and permissions."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
from build_gpu_module_v2 import build
root=Path(__file__).resolve().parents[1];out=root/'build/gpu-runtime-tests';out.mkdir(exist_ok=True)
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
fixture=out/'fixture.ndrv';metadata=build([root/'tests/host/gpu_v2_fixture.c'],fixture,0x1002,0x73ff,112)  # the fixture exercises the 104/112-byte legacy service prefixes; shipped modules use 136
exe=out/'image.exe'
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'tests/host/test_gpu_image.c'),str(root/'kernel/drivers/gpu/module_image.c'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe),str(fixture)],capture_output=True,text=True)
if result.returncode:raise SystemExit(result.stderr or result.stdout)
report=json.loads(result.stdout);report['fixture']=metadata
report['source_sha256']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in [
 'kernel/drivers/gpu/module_image.c','kernel/drivers/gpu/runtime.c','kernel/mm/vmm.c',
 'tools/gpu-driver/include/nexis_gpu_v2.h','scripts/build_gpu_module_v2.py','tests/host/test_gpu_image.c','tests/host/gpu_v2_fixture.c']}
report['kernel_runtime_hardware_tested']=False
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
