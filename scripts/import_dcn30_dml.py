#!/usr/bin/env python3
"""Import pinned AMD DCN30 math engine, preserving its algorithm and MIT notices.

NexisOS supplies only platform services and checked outer inputs. No Linux
driver binaries or firmware are imported. Edits below are reproducible.
"""
from pathlib import Path
import argparse,hashlib,json,re
root=Path(__file__).resolve().parents[1]
pins={
 'dc_features.h':'b260a3372550dabc84827b5d0b4fd041ac8a86a46cca37caceba2714ce304e41',
 'display_mode_enums.h':'eb0fe235de0aa7016916fda8554caa372ed2d6aa641e2834f13edd0c8d40520d',
 'display_mode_structs.h':'ca9502b61904f9ac7595ba9d737ed6c3dc9b1bbb5eb7793e5cef51d2e2a2ac19',
 'display_mode_vba.h':'c7848622b82f0ffd805fe8153856b61320e4953ab180d7874114c7fd3955c522',
 'display_mode_lib.h':'8ed6ea62183b272c5d438fa7e554e6a287ec9f5a064222483df302348d8e6b77',
 'display_mode_vba.c':'66f311d4c7967994351d7180ad6631efd0b014bc1252d731063447e0e411ef92',
 'dml_inline_defs.h':'3c57c7c9ffc4481282f72912efd72a7f0fa17da824f5eb1cb52f8ef96fcd059a',
 'dcn_calc_math.h':'197baec4d0046cd7d9989d59aca71f78eb44e805c8a76ff8e1a50a7ff3a2fd69',
 'dcn_calc_math.c':'0bec846715ee48891d74fef318e5569a08319248bd9185980aeb07385b31cf73',
 'display_rq_dlg_helpers.c':'646436b43c36fdb24b60aefd4a50099c72279cdc560af13ec90b20b412eac200',
 'display_rq_dlg_helpers.h':'861881e40c02858d33636303afb1452e8fc452c3fec107aaafd3d6c1862aebe7',
 'display_mode_vba_30.c':'0b3ea50d4a01e8b76c4e513bf43e5141d7b0f49d1ef5bf8176cdd58bc87c048e',
 'display_mode_vba_30.h':'0d8d4f7537621ad4e4f3206be3f7d144a46a84ea538a764b4003558b507493e4',
 'display_rq_dlg_calc_30.c':'7376a14096a8f2444dbf2f0fde0f51e3f65e59b1ca9e9e5d1dd5ac0fa88e5e3d',
 'display_rq_dlg_calc_30.h':'83a7d36210a9506b92733571064c396e3d1f5216c61b97c938d2ceabf48acdde',
}
def generate(reference):
 target=root/'tools/gpu-driver/amd/dml';target.mkdir(exist_ok=True)
 evidence={}
 for name,expected in pins.items():
  data=(reference/name).read_bytes();assert hashlib.sha256(data).hexdigest()==expected,name
  text=data.decode().replace('\r\n','\n')
  text=text.replace('#include "dm_services.h"','#include "nexis_dml_port.h"').replace('#include "dc.h"','#include "nexis_dml_port.h"').replace('#include "os_types.h"','#include "nexis_dml_port.h"')
  if name=='display_mode_structs.h':text=text.replace('#include "dc_features.h"','#include "nexis_dml_port.h"\n#include "dc_features.h"')
  for include in ['display_mode_lib.h','display_mode_vba.h','dml_inline_defs.h','display_rq_dlg_helpers.h']:
   text=text.replace('#include "../'+include+'"','#include "'+include+'"')
  if name=='display_mode_vba_30.c':
   text='#if defined(__GNUC__)\n/* Upstream shares int/unsigned-int arrays, legal corresponding signed aliases. */\n#pragma GCC diagnostic ignored "-Wpointer-sign"\n#endif\n'+text
  if name=='dml_inline_defs.h':
   old='unsigned long long ix = *((unsigned long long *)&x);'
   assert text.count(old)==1;text=text.replace(old,'unsigned long long ix;\n\tmemcpy(&ix, &x, sizeof(ix)); /* defined bit copy; avoid strict-aliasing UB */')
  if name=='dcn_calc_math.c':
   old='int * const exp_ptr = (int *)(&a);\n\tint x = *exp_ptr;';assert text.count(old)==1
   text=text.replace(old,'int x;\n\tmemcpy(&x, &a, sizeof(x)); /* defined bit copy */')
   text=text.replace('*exp_ptr = x;','memcpy(&a, &x, sizeof(a));')
  text+='\n/* NexisOS portable adaptation: pinned Linux v6.12 '+name+' SHA256 '+expected+'; see scripts/import_dcn30_dml.py. */\n'
  path=target/name;path.write_text(text,encoding='utf-8',newline='\n')
  evidence[name]={'upstream_sha256':expected,'adapted_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 # ASIC buffer/latency model from AMD. Do not import placeholder clocks,
 # voltage state, memory channel geometry or VCO as hardware capabilities.
 name='dcn302_fpu.c';expected='094f6054d95b0812ced8396a9532b4ab36f892877916ece7e76ad303ade152b6'
 data=(reference/name).read_bytes();assert hashlib.sha256(data).hexdigest()==expected,name
 original=data.decode();notice=original[:original.index('*/')+2]
 def initializer(symbol):
  start=original.index('{',original.index(symbol+' ='));depth=1;end=start+1
  while depth:
   depth+=(original[end]=='{')-(original[end]=='}');end+=1
  return original[start:end]
 ip=initializer('dcn3_02_ip');soc=initializer('dcn3_02_soc')
 start=soc.index('.clock_limits');brace=soc.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(soc[end]=='{')-(soc[end]=='}');end+=1
 assert soc[end]==',';soc=soc[:start]+soc[end+1:]
 for field in ('min_dcfclk','num_states','dispclk_dppclk_vco_speed_mhz'):
  soc,count=re.subn(r'\.'+field+r'\s*=\s*[^,]+,','',soc);assert count==1,field
 text=notice+'\n/* ASIC model only. Every board/clock value is supplied separately. */\n'
 text+='static const ip_params_st dcn302_ip_template = '+ip+';\n'
 text+='static const soc_bounding_box_st dcn302_soc_template = '+soc+';\n'
 path=target/'dcn302_model.h';path.write_text(text,encoding='utf-8',newline='\n')
 evidence['dcn302_model.h']={'upstream_file':name,'upstream_sha256':expected,'adapted_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'excluded':'placeholder clocks/states, min DCFCLK, VCO'}
 (target/'upstream-sources.json').write_text(json.dumps({'source':'AMD Linux v6.12','license':'MIT; notices retained in every file','files':evidence},indent=2)+'\n')
 print(json.dumps({'imported_files':len(pins),'algorithm':'DCN30 complete VBA and RQ/DLG','target':str(target)}))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
