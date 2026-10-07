#!/usr/bin/env python3
"""Generate actual DCN302 HUBP RQ/DLG/TTU fields from pinned AMD source.

Extract inherited DCN3 programming routines; retain each register's real
HUBP/HUBPREQ/HUBPRET prefix, field mask/shift and source struct member.
"""
from pathlib import Path
import argparse,hashlib,json,re
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
pins={**expected_sources,
 'dcn20_hubp.c':'9671025823f63ba040274f9a2d455463340e26b67233c755f1116a58d97a64d3',
 'dcn21_hubp.c':'0614c3f5cd1d21e7b67a9c003d0b607fb21d9cf9699c154c0052cd2a5f3c8bcb',
 'dcn30_hubp.c':'41c5811597a5897a2774e9aa3f939552b400112bbce52b981417c6f06618f297',
}
def function(source,name):
 m=re.search(r'(?:^|\n)(?:static\s+)?(?:void|bool|uint32_t|unsigned)\s+'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{',source)
 assert m,('missing function definition',name)
 start=m.end()-1;depth=1;end=start+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def generate(reference):
 for name,expected in pins.items():
  assert hashlib.sha256((reference/name).read_bytes()).hexdigest()==expected,name
 source=(reference/'dcn302_offset.h').read_text();d=definitions(source)
 fields=definitions((reference/'dcn302_mask.h').read_text());bases=definitions((reference/'dimgrey_cavefish_ip_offset.h').read_text())
 rows=[];registers=[]
 actual_names={}
 for key in d:
  m=re.fullmatch(r'mm(HUBP|HUBPREQ|HUBPRET)([0-4])_(.+)',key)
  if m:actual_names.setdefault((m[3],int(m[2])),[]).append(key[2:])
 def reg_index(reg):
  if reg not in registers:registers.append(reg)
  return registers.index(reg)
 def actual(reg,pipe):
  matches=actual_names.get((reg,pipe),[])
  assert len(matches)==1,(reg,pipe,matches)
  return matches[0]
 def field(reg,name):
  values=[(fields[actual(reg,p)+'__'+name+'_MASK'],fields[actual(reg,p)+'__'+name+'__SHIFT']) for p in range(5)]
  assert all(v==values[0] for v in values),(reg,name)
  mask,shift=values[0];assert mask and not mask&((1<<shift)-1)
  v=mask>>shift;assert not v&(v+1),(reg,name,'not contiguous')
  return mask,shift
 for file,name in [('dcn21_hubp.c','hubp21_program_requestor'),('dcn20_hubp.c','hubp2_program_deadline'),('dcn30_hubp.c','hubp3_program_deadline'),('dcn20_hubp.c','hubp2_setup_interdependent')]:
  text=function((reference/file).read_text(),name)
  for call in re.finditer(r'REG_(SET|UPDATE)(?:_(\d+))?\s*\((.*?)\);',text,re.S):
   args=[a.strip() for a in call[3].split(',')];reg=args.pop(0)
   if call[1]=='SET':assert args.pop(0)=='0'
   assert len(args)==2*int(call[2] or 1)
   for at in range(0,len(args),2):
    f,value=args[at:at+2];m=re.fullmatch(r'(rq_regs|dlg_attr|ttu_attr)->([\w.]+)',value);assert m,(reg,f,value)
    member={'rq_regs':'rq','dlg_attr':'dlg','ttu_attr':'ttu'}[m[1]]+'.'+m[2]
    mask,shift=field(reg,f);rows.append((reg_index(reg),f,mask,shift,member))
 # Only the VREADY bit is owned in the mixed RW/status/W1C control register.
 # Debug bits are documented by hubp2_vready... and hubp3_init respectively.
 for reg in ('DCHUBP_CNTL','HUBPREQ_DEBUG_DB','HUBPREQ_DEBUG'):reg_index(reg)
 # Validate the actual DCN3 blank inheritance and the DCN2 TTU behavior;
 # DCN1's TTU-disable/post-blank drain must not be substituted.
 inherited=(reference/'dcn30_hubp.c').read_text()
 assert '.set_blank_regs = hubp2_set_blank_regs' in inherited
 blank=function((reference/'dcn20_hubp.c').read_text(),'hubp2_set_blank_regs')
 assert blank.index('REG_WAIT')<blank.index('REG_UPDATE_2') and re.search(r'HUBP_TTU_DISABLE,\s*0',blank)
 masks=[0]*len(registers)
 for n,f,mask,shift,member in rows:
  assert not masks[n]&mask,(registers[n],f,'overlap');masks[n]|=mask
 masks[reg_index('DCHUBP_CNTL')]=field('DCHUBP_CNTL','HUBP_VREADY_AT_OR_AFTER_VSYNC')[0]
 masks[reg_index('HUBPREQ_DEBUG_DB')]=1<<8;masks[reg_index('HUBPREQ_DEBUG')]=1<<26
 # Do not echo read status-clear bits into writes. Preserve all other bits.
 forbidden=[0]*len(registers);readonly=[0]*len(registers)
 for reg,ro,w1c in [
  ('DCHUBP_CNTL',['HUBP_NO_OUTSTANDING_REQ','HUBP_IN_BLANK','HUBP_XRQ_NO_OUTSTANDING_REQ','HUBP_TIMEOUT_STATUS','HUBP_UNDERFLOW_STATUS'],['HUBP_TIMEOUT_STATUS_CLEAR','HUBP_UNDERFLOW_CLEAR']),
  ('DCN_DMDATA_VM_CNTL',['DMDATA_VM_FAULT_STATUS','DMDATA_VM_UNDERFLOW_STATUS','DMDATA_VM_LATE_STATUS','DMDATA_VM_DONE'],['DMDATA_VM_FAULT_STATUS_CLEAR','DMDATA_VM_UNDERFLOW_STATUS_CLEAR']),
 ]:
  assert reg in registers; n=reg_index(reg)
  for f in ro:readonly[n]|=field(reg,f)[0]
  for f in w1c:forbidden[n]|=field(reg,f)[0]
 def address(reg,p):
  n='mm'+actual(reg,p);v=(bases['DCN_BASE__INST0_SEG'+str(d[n+'_BASE_IDX'])]+d[n])*4
  assert v<1024*1024 and not v&3;return v
 text=source[:source.index('*/')+2]+'\n/* Generated pinned AMD DCN302 HUBP fields; never infer prefixes/widths. */\n'
 text+='#ifndef NEXIS_DCN302_HUBP_REGS_H\n#define NEXIS_DCN302_HUBP_REGS_H\n#include "dcn302_dml.h"\n#include <stddef.h>\n'
 text+='enum dcn302_hubp_register {\n'+''.join(' DCN302_HUBP_R_'+r+',\n' for r in registers)+' DCN302_HUBP_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_hubp_register_bytes[5][DCN302_HUBP_REGISTER_COUNT]={\n'
 for p in range(5):text+=' {'+','.join('0x%05xu'%address(r,p) for r in registers)+'},\n'
 text+='};\n'
 for name,values in [('owned',masks),('forbidden',forbidden),('readonly',readonly)]:
  text+='static const uint32_t dcn302_hubp_'+name+'[DCN302_HUBP_REGISTER_COUNT]={'+','.join('0x%08xu'%v for v in values)+'};\n'
 text+='typedef struct {uint32_t mask;uint16_t offset;uint8_t reg,shift;} dcn302_hubp_field;\n'
 text+='static const dcn302_hubp_field dcn302_hubp_fields[]={\n'
 for n,f,mask,shift,member in rows:text+=f' {{0x{mask:08x}u,offsetof(dcn302_dml_output,{member}),{n},{shift}}}, /* {registers[n]}.{f} */\n'
 text+='};\n#define DCN302_HUBP_FIELD_COUNT (sizeof(dcn302_hubp_fields)/sizeof(dcn302_hubp_fields[0]))\n'
 for reg,f in [('DCHUBP_CNTL','HUBP_BLANK_EN'),('DCHUBP_CNTL','HUBP_TTU_DISABLE'),('DCHUBP_CNTL','HUBP_NO_OUTSTANDING_REQ'),('DCHUBP_CNTL','HUBP_VREADY_AT_OR_AFTER_VSYNC'),('DCHUBP_CNTL','HUBP_UNDERFLOW_STATUS'),('DCHUBP_CNTL','HUBP_TIMEOUT_STATUS'),('DCHUBP_CNTL','HUBP_DISABLE'),
  ('HUBP_CLK_CNTL','HUBP_CLOCK_ENABLE'),('HUBP_CLK_CNTL','HUBP_DISPCLK_R_CLOCK_ON'),('HUBP_CLK_CNTL','HUBP_DPPCLK_G_CLOCK_ON'),('HUBP_CLK_CNTL','HUBP_DCFCLK_R_CLOCK_ON'),('HUBP_CLK_CNTL','HUBP_DCFCLK_G_CLOCK_ON')]:
  mask,shift=field(reg,f);text+=f'#define DCN302_HUBP_{f}_MASK 0x{mask:08x}u\n#define DCN302_HUBP_{f}_SHIFT {shift}u\n'
 text+='static const uint32_t dcn302_hubp_clock_bytes[5]={'+','.join('0x%05xu'%address('HUBP_CLK_CNTL',p) for p in range(5))+'};\n#endif\n'
 target=root/'tools/gpu-driver/amd/dcn302_hubp_regs.h';target.write_text(text,encoding='utf-8',newline='\n')
 evidence={'source':'AMD Linux v6.12','upstream_sha256':pins,'registers':len(registers),'fields':len(rows),'blank_inheritance':'hubp3 -> hubp2_set_blank_regs; drain before blank; TTU_DISABLE=0','generated_sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
 (root/'build/dcn302-hubp-register-sources.json').write_text(json.dumps(evidence,indent=2)+'\n')
 print(json.dumps(evidence))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
