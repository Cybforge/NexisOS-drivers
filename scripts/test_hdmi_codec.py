#!/usr/bin/env python3
"""Exercise the actual native HDMI codec source against a strict register model."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/hdmi-tests';out.mkdir(exist_ok=True)
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
exe=out/'test_hdmi_codec.exe'
sources=[root/'tests/host/test_hdmi_codec.c',root/'kernel/drivers/audio/hdmi.c']
subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[str(p) for p in sources],'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True)
if result.returncode:
    raise SystemExit(result.stderr or result.stdout or 'HDMI codec model failed')
report=json.loads(result.stdout)
report['source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
