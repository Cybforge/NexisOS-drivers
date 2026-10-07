#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/atom-memory-tests';out.mkdir(exist_ok=True)
sources=['tests/host/test_atom_memory.c','tools/gpu-driver/amd/atom_memory.c','tools/gpu-driver/amd/atom_tables.c']
flags=['-std=c11','-Wall','-Wextra','-Werror']
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
reports=[]
for checked in (False,True):
 cc=[zig,'cc','-target','x86_64-windows-gnu','-O1','-fsanitize=undefined','-fno-sanitize-recover=all'] if checked else [gcc,'-O2']
 exe=out/('memory-ubsan.exe' if checked else 'memory.exe')
 subprocess.run(cc+flags+[str(root/s) for s in sources]+['-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,check=True);reports.append(json.loads(result.stdout))
assert reports[0]==reports[1];report=reports[0];report['undefined_behavior_sanitizer_passed']=True
report['source_sha256']={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+['tools/gpu-driver/amd/atom_memory.h','tools/gpu-driver/amd/atom_tables.h','scripts/test_atom_memory.py']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
