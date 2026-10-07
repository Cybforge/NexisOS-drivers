#!/usr/bin/env python3
"""Test AMD DCN30 math as native code and an actual retained PIC math harness.

The harness is never installed/published as a physical card driver.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
from build_gpu_module_v2 import build
root=Path(__file__).resolve().parents[1];out=root/'build/dcn302-dml-tests';out.mkdir(exist_ok=True)
native=['tools/gpu-driver/amd/dcn302_dml.c','tools/gpu-driver/amd/dml/nexis_dml_port.c',
 'tools/gpu-driver/amd/dml/display_mode_vba.c','tools/gpu-driver/amd/dml/display_mode_vba_30.c',
 'tools/gpu-driver/amd/dml/display_rq_dlg_calc_30.c','tools/gpu-driver/amd/dml/dcn_calc_math.c',
 'tools/gpu-driver/amd/dml/display_rq_dlg_helpers.c']
artifact=out/'math-harness.ndrv'
metadata=build([root/s for s in native+['tests/host/dml_pic_module.c','tools/gpu-driver/common/memory.c']],artifact,0x1002,0x73ff)
flags=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-pointer-sign','-I'+str(root/'tools/gpu-driver/amd/dml')]
sources=['tests/host/test_dcn302_dml.c','kernel/drivers/gpu/module_image.c']+native
scope_sources=['tests/host/test_dml_scope.c','tools/gpu-driver/amd/dml/nexis_dml_port.c']
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
reports=[];scopes=[]
for checked in (False,True):
 cc=[zig,'cc','-target','x86_64-windows-gnu','-O1','-fsanitize=undefined','-fno-sanitize-recover=all'] if checked else [gcc,'-O2']
 exe=out/('dml-ubsan.exe' if checked else 'dml.exe')
 subprocess.run(cc+flags+[str(root/s) for s in sources]+['-o',str(exe)],check=True)
 result=subprocess.run([str(exe),str(artifact)],capture_output=True,text=True)
 if result.returncode:raise SystemExit(f'DML exit {result.returncode}: '+result.stderr+result.stdout)
 reports.append(json.loads(result.stdout))
 exe=out/('scope-ubsan.exe' if checked else 'scope.exe')
 subprocess.run(cc+flags+[str(root/s) for s in scope_sources]+['-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 if result.returncode:raise SystemExit(f'FP scope exit {result.returncode}: '+result.stderr+result.stdout)
 scopes.append(json.loads(result.stdout))
assert reports[0]==reports[1] and scopes[0]==scopes[1]
report=reports[0];report.update(undefined_behavior_sanitizer_passed=True,fp_scope=scopes[0],math_harness=metadata,distributed=False,full_rx6600_driver_complete=False,physical_gpu_drivers_complete=0)
files=sources+scope_sources+['tests/host/dml_pic_module.c','tests/host/dml_pic_module.h',
 'tools/gpu-driver/common/memory.c','tools/gpu-driver/amd/dcn302_dml.h','scripts/test_dcn302_dml.py',
 'scripts/import_dcn30_dml.py','scripts/build_gpu_module_v2.py']
files+=['tools/gpu-driver/amd/dml/'+p.name for p in (root/'tools/gpu-driver/amd/dml').iterdir() if p.suffix in ('.h','.json')]
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in dict.fromkeys(files)}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
