#!/usr/bin/env python3
"""Generate exact Navi23 HUBBUB watermarks/reference/policy, pinned AMD v6.12."""
from pathlib import Path
import argparse,hashlib,json
from generate_dcn302_regs import definitions,expected_sources
from generate_dcn302_hubp_regs import function
root=Path(__file__).resolve().parents[1]
pins={**expected_sources,
 'dcn30_hubbub.c':'47ab0d94c3f456d11ab1ac42d1cc6d3fffd61bb9c252f0aa1fef5aa9cfc6a790',
 'dcn21_hubbub.c':'edee9e3284b019d0ecd0671bf5bbaa7a5cbae09f05c6638beb442db03b665197',
 'dcn10_hubbub.c':'064c85bbc900997e7065720740c03f7759b85d190f839043c2b2feb455496a82',
 'dcn20_hubbub.c':'402bf2e371a6503c091f79867e1464ef71ef980e92c6c348b42718ee624cfbde',
 'dcn20_dccg.c':'e4a840c109ae9d00f796de6c13610cffe97c61c4b1c025cc02d686fef815753e',
 'dcn30_dccg.c':'4d73ceb9124885609b4ee8e44af7f68c87e262e980505a3f3485f1cbc403a951',
}
def generate(reference):
 for name,expected in pins.items():assert hashlib.sha256((reference/name).read_bytes()).hexdigest()==expected,name
 source=(reference/'dcn302_offset.h').read_text();offsets=definitions(source)
 fields=definitions((reference/'dcn302_mask.h').read_text());bases=definitions((reference/'dimgrey_cavefish_ip_offset.h').read_text())
 upstream=(reference/'dcn30_hubbub.c').read_text()
 assert '.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq' in upstream
 assert '.get_dccg_ref_freq = dccg2_get_dccg_ref_freq' in (reference/'dcn30_dccg.c').read_text()
 for func in ('hubbub21_program_urgent_watermarks','hubbub21_program_stutter_watermarks','hubbub21_program_pstate_watermarks'):assert func in function(upstream,'hubbub3_program_watermarks')
 registers=['DCHUBBUB_ARB_DRAM_STATE_CNTL'];rows=[]
 def add(reg,f,value,ns=False):
  assert reg in upstream or reg in (reference/'dcn21_hubbub.c').read_text() or reg in (reference/'dcn10_hubbub.c').read_text()
  if reg not in registers:registers.append(reg)
  mask,shift=fields[reg+'__'+f+'_MASK'],fields[reg+'__'+f+'__SHIFT'];v=mask>>shift
  assert mask and not mask&((1<<shift)-1) and not v&(v+1)
  rows.append((registers.index(reg),mask,shift,value,ns))
 for suffix in 'ABCD':
  for prefix,member,ns,vm in [
   ('DATA_URGENCY_WATERMARK','urgent_ns',True,'VM_ROW_URGENCY_WATERMARK'),
   ('FRAC_URG_BW_FLIP','frac_urg_flip',False,None),('FRAC_URG_BW_NOM','frac_urg_nom',False,None),
   ('REFCYC_PER_TRIP_TO_MEMORY','memory_trip_ns',True,None),
   ('ALLOW_SR_ENTER_WATERMARK','stutter_enter_exit_ns',True,'VM_ROW_ALLOW_SR_ENTER_WATERMARK'),
   ('ALLOW_SR_EXIT_WATERMARK','stutter_exit_ns',True,'VM_ROW_ALLOW_SR_EXIT_WATERMARK'),
   ('ALLOW_DRAM_CLK_CHANGE_WATERMARK','dram_change_ns',True,'VM_ROW_ALLOW_DRAM_CLK_CHANGE_WATERMARK')]:
   reg='DCHUBBUB_ARB_'+prefix+'_'+suffix;add(reg,reg,member,ns)
   if vm:add(reg,'DCHUBBUB_ARB_'+vm+'_'+suffix,member,ns)
 add('DCHUBBUB_ARB_SAT_LEVEL','DCHUBBUB_ARB_SAT_LEVEL','sat_cycles')
 add('DCHUBBUB_ARB_DF_REQ_OUTSTAND','DCHUBBUB_ARB_MIN_REQ_OUTSTAND','min_outstanding')
 for f,member in [('ALLOW_SELF_REFRESH_FORCE_VALUE','sr_value'),('ALLOW_SELF_REFRESH_FORCE_ENABLE','sr_force'),('ALLOW_PSTATE_CHANGE_FORCE_VALUE','pstate_value'),('ALLOW_PSTATE_CHANGE_FORCE_ENABLE','pstate_force')]:add(registers[0],'DCHUBBUB_ARB_'+f,member)
 def address(reg):
  n='mm'+reg;v=(bases['DCN_BASE__INST0_SEG'+str(offsets[n+'_BASE_IDX'])]+offsets[n])*4
  assert v<1024*1024 and not v&3;return v
 masks=[0]*len(registers)
 for n,mask,_,_,_ in rows:assert not masks[n]&mask;masks[n]|=mask
 text=source[:source.index('*/')+2]+'\n/* Generated pinned AMD DCN302 HUBBUB fields; exact native widths. */\n'
 text+='#ifndef NEXIS_DCN302_HUBBUB_REGS_H\n#define NEXIS_DCN302_HUBBUB_REGS_H\n#include <stdint.h>\n'
 text+='enum dcn302_hubbub_register {\n'+''.join(' DCN302_HUBBUB_R_'+r+',\n' for r in registers)+' DCN302_HUBBUB_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_hubbub_register_bytes[]={'+','.join('0x%05xu'%address(r) for r in registers)+'};\n'
 text+='static const uint32_t dcn302_hubbub_owned[]={'+','.join('0x%08xu'%v for v in masks)+'};\n'
 for reg,field,prefix in [('REFCLK_CNTL','REFCLK_CLOCK_EN','REF_ENABLE'),('REFCLK_CNTL','REFCLK_SRC_SEL','REF_SELECT'),('DCHUBBUB_GLOBAL_TIMER_CNTL','DCHUBBUB_GLOBAL_TIMER_REFDIV','TIMER_DIV'),('DCHUBBUB_GLOBAL_TIMER_CNTL','DCHUBBUB_GLOBAL_TIMER_ENABLE','TIMER_ENABLE')]:
  text+=f'#define DCN302_HUBBUB_{prefix}_MASK 0x{fields[reg+"__"+field+"_MASK"]:08x}u\n#define DCN302_HUBBUB_{prefix}_SHIFT {fields[reg+"__"+field+"__SHIFT"]}u\n'
 text+=f'#define DCN302_HUBBUB_REF_BYTES 0x{address("REFCLK_CNTL"):05x}u\n#define DCN302_HUBBUB_TIMER_BYTES 0x{address("DCHUBBUB_GLOBAL_TIMER_CNTL"):05x}u\n'
 # Caller defines dcn302_hubbub_values before expanding this native field list.
 text+='#define DCN302_HUBBUB_FIELDS(X) \\\n'+ ' \\\n'.join(f' X({n},0x{mask:08x}u,{shift},{member},{1 if ns else 0})' for n,mask,shift,member,ns in rows)+'\n#endif\n'
 path=root/'tools/gpu-driver/amd/dcn302_hubbub_regs.h';path.write_text(text,encoding='utf-8',newline='\n')
 evidence={'source':'AMD Linux v6.12','upstream_sha256':pins,'registers':len(registers),'fields':len(rows),'generated_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 (root/'build/dcn302-hubbub-register-sources.json').write_text(json.dumps(evidence,indent=2)+'\n');print(json.dumps(evidence))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
