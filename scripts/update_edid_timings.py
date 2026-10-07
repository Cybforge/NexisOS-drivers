#!/usr/bin/env python3
"""Extract MIT-licensed timing tables from pinned Linux v6.12 DRM source.

The parser and driver are native NexisOS code. Only numeric DMT/CTA timings
are imported; the source's MIT notice is retained in the generated header.
"""
from pathlib import Path
import hashlib,re,urllib.request
root=Path(__file__).resolve().parents[1]
url='https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/drm_edid.c'
data=urllib.request.urlopen(url,timeout=30).read()
text=data.decode('utf-8');license=text[:text.index('*/')+2]
assert 'Permission is hereby granted' in license and 'sub license' in license
pattern=r'DRM_MODE\("[^"\n]+",\s*DRM_MODE_TYPE_DRIVER,\s*([\d,\s]+),\s*(DRM_MODE_FLAG_[A-Z_\s|]+)\)'
def table(name):
    body=text.split('static const struct drm_display_mode '+name+'[] = {',1)[1].split('\n};',1)[0]
    rows=[]
    for match in re.finditer(pattern,body):
        nums=[int(s) for s in match[1].split(',')];assert len(nums)==11
        clock,ha,hs,he,ht,skew,va,vs,ve,vt,scan=nums
        flags=match[2];assert skew==0 and scan==0
        f=(1 if 'DRM_MODE_FLAG_PHSYNC' in flags else 0)|(2 if 'DRM_MODE_FLAG_PVSYNC' in flags else 0)|(4 if 'DRM_MODE_FLAG_INTERLACE' in flags else 0)|(8 if 'DRM_MODE_FLAG_DBLCLK' in flags else 0)
        rows.append('{'+','.join(map(str,[clock,ha,hs,he,ht,va,vs,ve,vt,f]))+'}')
    return rows
tables={name:table(name) for name in ['drm_dmt_modes','edid_cea_modes_1','edid_cea_modes_193']}
assert len(tables['drm_dmt_modes'])==88 and len(tables['edid_cea_modes_1'])==127 and len(tables['edid_cea_modes_193'])==27
result=license+'\n/* Numeric timing tables extracted from '+url+'\n * SHA-256 '+hashlib.sha256(data).hexdigest()+'\n * Regenerate with scripts/update_edid_timings.py. */\n'
for name,rows in tables.items():
    result+='static const edid_timing '+name+'[] = {\n '+',\n '.join(rows)+'\n};\n'
(root/'kernel/drivers/gpu/edid_timings.h').write_text(result,encoding='utf-8')
print('Imported',sum(map(len,tables.values())),'DMT/CTA timings, MIT notice retained')
