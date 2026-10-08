#!/usr/bin/env python3
"""Exact native Navi23 timing/update-lock ownership and buffering definitions."""
from pathlib import Path
import argparse,hashlib,json
from generate_dcn302_regs import definitions,expected_sources,registers,fields
from generate_dcn302_hubp_regs import function
root=Path(__file__).resolve().parents[1]
pins={**expected_sources,'dcn10_optc.c':'ebd9f31f41e3cfc5bee363b0ebcf21774be5dfb79136366e94f1dbfecd0e10a8',
 'dcn30_optc.c':'7e751acb0765953ecfbafc7f1123b4d67d67d4af2190c45a7d96f700563b47fc'}
def generate(ref):
 for name,sha in pins.items():assert hashlib.sha256((ref/name).read_bytes()).hexdigest()==sha,name
 src=(ref/'dcn302_offset.h').read_text();offsets=definitions(src)
 masks=definitions((ref/'dcn302_mask.h').read_text());bases=definitions((ref/'dimgrey_cavefish_ip_offset.h').read_text())
 upstream=(ref/'dcn30_optc.c').read_text()
 for inherited in ['.program_timing = optc1_program_timing','.program_global_sync = optc1_program_global_sync','.lock = optc3_lock','.unlock = optc1_unlock']:assert inherited in upstream,inherited
 assert 'OTG_GLOBAL_CONTROL2' in function(upstream,'optc3_lock')
 assert 'OTG_DRR_TIMING_DBUF_UPDATE_MODE' in function(upstream,'optc3_set_timing_double_buffer')
 extra={'GLOBAL0':'OTG{i}_OTG_GLOBAL_CONTROL0','GLOBAL1':'OTG{i}_OTG_GLOBAL_CONTROL1',
        'GLOBAL4':'OTG{i}_OTG_GLOBAL_CONTROL4','DBUF':'OTG{i}_OTG_DOUBLE_BUFFER_CONTROL'}
 allregs={**registers,**extra};names=list(allregs);owned=[0]*len(names)
 def native(reg,field):
  values=[]
  for p in range(5):
   n=allregs[reg].format(i=p)+'__'+field.format(i=p)
   values.append((masks[n+'_MASK'],masks[n+'__SHIFT']))
  assert all(v==values[0] for v in values)
  return values[0]
 for f in ['H_TOTAL','H_BLANK_START','H_BLANK_END','H_SYNC_START','H_SYNC_END','H_POL','V_TOTAL',
  'V_MIN','V_MAX','V_BLANK_START','V_BLANK_END','V_SYNC_START','V_SYNC_END','V_POL',
  'INTERLACE','START_POINT','FIELD_NUMBER','V_STARTUP','V_UPDATE_OFFSET','V_UPDATE_WIDTH','V_READY','VTG_INIT','VTG_FP2']:
  reg,field=fields[f];mask,_=native(reg,field);owned[names.index(reg)]|=mask
 # Fixed-rate policy: native selectors/mid/mask/force-lock controls. Preserve
 # the unrelated DRR event-period field; no assumed all-RW register mask.
 for field in ['OTG_V_TOTAL_MIN_SEL','OTG_V_TOTAL_MAX_SEL','OTG_VTOTAL_MID_REPLACING_MAX_EN',
  'OTG_VTOTAL_MID_REPLACING_MIN_EN','OTG_FORCE_LOCK_ON_EVENT','OTG_SET_V_TOTAL_MIN_MASK_EN',
  'OTG_VTOTAL_MID_FRAME_NUM','OTG_SET_V_TOTAL_MIN_MASK']:
  owned[names.index('V_CONTROL')]|=native('V_CONTROL',field)[0]
 assert 'OTG_FORCE_LOCK_ON_EVENT' in function((ref/'dcn10_optc.c').read_text(),'optc1_set_drr')
 macros={}
 for reg,field,key in [('GLOBAL0','MASTER_UPDATE_LOCK_DB_START_X','DB_START_X'),('GLOBAL0','MASTER_UPDATE_LOCK_DB_END_X','DB_END_X'),
  ('GLOBAL0','MASTER_UPDATE_LOCK_DB_EN','DB_ENABLE'),('GLOBAL1','MASTER_UPDATE_LOCK_DB_START_Y','DB_START_Y'),
  ('GLOBAL1','MASTER_UPDATE_LOCK_DB_END_Y','DB_END_Y'),('GLOBAL2','GLOBAL_UPDATE_LOCK_EN','GLOBAL_LOCK_ENABLE'),
  ('GLOBAL2','OTG_MASTER_UPDATE_LOCK_SEL','LOCK_SELECT'),('LOCK','OTG_MASTER_UPDATE_LOCK','LOCK'),
  ('DBUF','OTG_DRR_TIMING_DBUF_UPDATE_MODE','DRR_MODE')]:
  mask,shift=native(reg,field);owned[names.index(reg)]|=mask;macros[key]=(mask,shift)
 dbprefix='OTG0_OTG_DOUBLE_BUFFER_CONTROL__'
 pending=sum(v for n,v in masks.items() if n.startswith(dbprefix) and n.endswith('_MASK') and 'PENDING' in n)
 instant,_=native('DBUF','OTG_UPDATE_INSTANTLY');macros['DBUF_PENDING']=(pending,0);macros['DBUF_INSTANT']=(instant,0)
 # Parent owns enable/disable sequencing, not these timing configuration words.
 excluded=[0]*len(names)
 excluded[names.index('CONTROL')]=native('CONTROL','OTG_CURRENT_MASTER_EN_STATE')[0]|native('CONTROL','OTG_MASTER_EN')[0]|native('CONTROL','OTG_DISABLE_POINT_CNTL')[0]
 excluded[names.index('VTG')]=native('VTG','VTG{i}_ENABLE')[0]
 excluded[names.index('CLOCK')]=native('CLOCK','OTG_BUSY')[0]|native('CLOCK','OTG_CLOCK_ON')[0]
 excluded[names.index('LOCK')]=native('LOCK','UPDATE_LOCK_STATUS')[0]
 excluded[names.index('DBUF')]=pending|instant
 excluded[names.index('FRAME_COUNT')]=0xffffffff
 write_excluded=excluded.copy()
 write_excluded[names.index('CONTROL')]=native('CONTROL','OTG_CURRENT_MASTER_EN_STATE')[0]
 write_excluded[names.index('VTG')]=0
 for n,mask in enumerate(owned):assert not mask&excluded[n]
 text=src[:src.index('*/')+2]+'\n/* Pinned native DCN3 timing/lock/buffer ownership. */\n'
 text+='#ifndef NEXIS_DCN302_TIMING_REGS_H\n#define NEXIS_DCN302_TIMING_REGS_H\n#include "dcn302_regs.h"\n'
 text+='enum {\n'+''.join(f' DCN302_TIMING_R_{key}={names.index(key)},\n' for key in extra)+f' DCN302_TIMING_REGISTER_COUNT={len(names)}\n}};\n'
 text+='static const uint32_t dcn302_timing_register_bytes[5][DCN302_TIMING_REGISTER_COUNT]={\n'
 for p in range(5):
  addresses=[]
  for template in allregs.values():
   n='mm'+template.format(i=p);addr=(bases['DCN_BASE__INST0_SEG'+str(offsets[n+'_BASE_IDX'])]+offsets[n])*4
   assert addr<1024*1024 and not addr&3;addresses.append(addr)
  text+=' {'+','.join(f'0x{a:05x}u' for a in addresses)+'},\n'
 text+='};\n'
 for key,values in [('owned',owned),('excluded',excluded),('write_excluded',write_excluded)]:text+='static const uint32_t dcn302_timing_'+key+'[]={'+','.join(f'0x{v:08x}u' for v in values)+'};\n'
 for name,(mask,shift) in macros.items():text+=f'#define DCN302_TIMING_{name}_MASK 0x{mask:08x}u\n#define DCN302_TIMING_{name}_SHIFT {shift}u\n'
 text+='#endif\n';path=root/'tools/gpu-driver/amd/dcn302_timing_regs.h';path.write_text(text,encoding='utf-8',newline='\n')
 evidence={'source':'AMD Linux v6.12','upstream_sha256':pins,'registers':len(names),'owned_registers':sum(bool(v) for v in owned),
  'native_dcn3_global2_lock_verified':True,'native_drr_mode_field_verified':True,'generated_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 (root/'build/dcn302-timing-register-sources.json').write_text(json.dumps(evidence,indent=2)+'\n');print(json.dumps(evidence))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
