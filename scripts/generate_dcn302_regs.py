#!/usr/bin/env python3
"""Extract the exact Navi23/DCN302 OTG register subset from pinned AMD headers.

Supply Linux v6.12 source headers with --reference; no network or hardware access.
"""
from pathlib import Path
import argparse,hashlib,json,re
root=Path(__file__).resolve().parents[1]
expected_sources={
 'dcn302_offset.h':'052c86b51b20d93b99c95b2556fd68b4ecd5ae2388c59a8b2b08a4a00e2ea780',
 'dcn302_mask.h':'8491525dd88e7ee6fec5f926ee93ee1a2d16953428637325bfd2aaa98c77822b',
 'dimgrey_cavefish_ip_offset.h':'cd6b042e6ec1e8f71549828fa225182b0bb00c7be6b556ad0d3591d200afedf2',
}
registers={
 'CONTROL':'OTG{i}_OTG_CONTROL','CLOCK':'OTG{i}_OTG_CLOCK_CONTROL',
 'H_TOTAL':'OTG{i}_OTG_H_TOTAL','H_BLANK':'OTG{i}_OTG_H_BLANK_START_END',
 'H_SYNC':'OTG{i}_OTG_H_SYNC_A','H_POL':'OTG{i}_OTG_H_SYNC_A_CNTL',
 'H_DIV':'OTG{i}_OTG_H_TIMING_CNTL','V_TOTAL':'OTG{i}_OTG_V_TOTAL',
 'V_MIN':'OTG{i}_OTG_V_TOTAL_MIN','V_MAX':'OTG{i}_OTG_V_TOTAL_MAX',
 'V_CONTROL':'OTG{i}_OTG_V_TOTAL_CONTROL','V_BLANK':'OTG{i}_OTG_V_BLANK_START_END',
 'V_SYNC':'OTG{i}_OTG_V_SYNC_A','V_POL':'OTG{i}_OTG_V_SYNC_A_CNTL',
 'INTERLACE':'OTG{i}_OTG_INTERLACE_CONTROL','FRAME_COUNT':'OTG{i}_OTG_STATUS_FRAME_COUNT',
 'LOCK':'OTG{i}_OTG_MASTER_UPDATE_LOCK','GLOBAL2':'OTG{i}_OTG_GLOBAL_CONTROL2',
 'V_STARTUP':'OTG{i}_OTG_VSTARTUP_PARAM','V_UPDATE':'OTG{i}_OTG_VUPDATE_PARAM',
 'V_READY':'OTG{i}_OTG_VREADY_PARAM','VTG':'VTG{i}_CONTROL',
 'FORMAT':'ODM{i}_OPTC_DATA_FORMAT_CONTROL','SOURCE':'ODM{i}_OPTC_DATA_SOURCE_SELECT',
}
fields={
 'MASTER_ENABLE':('CONTROL','OTG_MASTER_EN'),'MASTER_ACTIVE':('CONTROL','OTG_CURRENT_MASTER_EN_STATE'),
 'START_POINT':('CONTROL','OTG_START_POINT_CNTL'),'FIELD_NUMBER':('CONTROL','OTG_FIELD_NUMBER_CNTL'),
 'DISABLE_POINT':('CONTROL','OTG_DISABLE_POINT_CNTL'),'BUSY':('CLOCK','OTG_BUSY'),
 'CLOCK_ENABLE':('CLOCK','OTG_CLOCK_EN'),'CLOCK_ON':('CLOCK','OTG_CLOCK_ON'),'SOFT_RESET':('CLOCK','OTG_SOFT_RESET'),
 'H_TOTAL':('H_TOTAL','OTG_H_TOTAL'),'H_BLANK_START':('H_BLANK','OTG_H_BLANK_START'),'H_BLANK_END':('H_BLANK','OTG_H_BLANK_END'),
 'H_SYNC_START':('H_SYNC','OTG_H_SYNC_A_START'),'H_SYNC_END':('H_SYNC','OTG_H_SYNC_A_END'),'H_POL':('H_POL','OTG_H_SYNC_A_POL'),
 'H_DIV':('H_DIV','OTG_H_TIMING_DIV_MODE'),'V_TOTAL':('V_TOTAL','OTG_V_TOTAL'),
 'V_MIN':('V_MIN','OTG_V_TOTAL_MIN'),'V_MAX':('V_MAX','OTG_V_TOTAL_MAX'),
 'V_MIN_SELECT':('V_CONTROL','OTG_V_TOTAL_MIN_SEL'),'V_MAX_SELECT':('V_CONTROL','OTG_V_TOTAL_MAX_SEL'),
 'V_MID_MAX':('V_CONTROL','OTG_VTOTAL_MID_REPLACING_MAX_EN'),'V_MID_MIN':('V_CONTROL','OTG_VTOTAL_MID_REPLACING_MIN_EN'),
 'V_BLANK_START':('V_BLANK','OTG_V_BLANK_START'),'V_BLANK_END':('V_BLANK','OTG_V_BLANK_END'),
 'V_SYNC_START':('V_SYNC','OTG_V_SYNC_A_START'),'V_SYNC_END':('V_SYNC','OTG_V_SYNC_A_END'),'V_POL':('V_POL','OTG_V_SYNC_A_POL'),
 'INTERLACE':('INTERLACE','OTG_INTERLACE_ENABLE'),'FRAME_COUNT':('FRAME_COUNT','OTG_FRAME_COUNT'),
 'LOCK':('LOCK','OTG_MASTER_UPDATE_LOCK'),'LOCK_STATUS':('LOCK','UPDATE_LOCK_STATUS'),
 'LOCK_SELECT':('GLOBAL2','OTG_MASTER_UPDATE_LOCK_SEL'),'V_STARTUP':('V_STARTUP','VSTARTUP_START'),
 'V_UPDATE_OFFSET':('V_UPDATE','VUPDATE_OFFSET'),'V_UPDATE_WIDTH':('V_UPDATE','VUPDATE_WIDTH'),
 'V_READY':('V_READY','VREADY_OFFSET'),'VTG_INIT':('VTG','VTG{i}_VCOUNT_INIT'),
 'VTG_FP2':('VTG','VTG{i}_FP2'),'VTG_ENABLE':('VTG','VTG{i}_ENABLE'),
 'FORMAT':('FORMAT','OPTC_DATA_FORMAT'),'DSC':('FORMAT','OPTC_DSC_MODE'),
 'SEGMENTS':('SOURCE','OPTC_NUM_OF_INPUT_SEGMENT'),'SEG0':('SOURCE','OPTC_SEG0_SRC_SEL'),'SEG1':('SOURCE','OPTC_SEG1_SRC_SEL'),
}
def definitions(s):
 return {m[1]:int(m[2],0) for m in re.finditer(r'^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*$',s,re.M)}
def generate(directory):
 for name,expected in expected_sources.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:
   raise ValueError('Not the pinned AMD Linux v6.12 source: '+name)
 offset=(directory/'dcn302_offset.h').read_text();mask=(directory/'dcn302_mask.h').read_text();ip=(directory/'dimgrey_cavefish_ip_offset.h').read_text()
 d=definitions(offset);f=definitions(mask);bases=definitions(ip)
 assert bases['DCN_BASE__INST0_SEG2']==0x34c0
 text=offset[:offset.index('*/')+2]+'\n/* Generated subset of AMD Linux v6.12 DCN302/Navi23 definitions.\n'
 for name in ['dcn302_offset.h','dcn302_mask.h','dimgrey_cavefish_ip_offset.h']:
  text+=' * '+name+' SHA256 '+hashlib.sha256((directory/name).read_bytes()).hexdigest()+'\n'
 text+=' */\n#ifndef NEXIS_DCN302_REGS_H\n#define NEXIS_DCN302_REGS_H\n#include <stdint.h>\n'
 text+='enum dcn302_register {\n'+''.join(' DCN302_R_'+key+',\n' for key in registers)+' DCN302_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_register_bytes[5][DCN302_REGISTER_COUNT]={\n'
 for pipe in range(5):
  values=[]
  for template in registers.values():
   name='mm'+template.format(i=pipe);seg=d[name+'_BASE_IDX'];address=(bases['DCN_BASE__INST0_SEG'+str(seg)]+d[name])*4
   assert address<1024*1024 and not address&3;values.append(address)
  text+=' {'+','.join('0x%05xu'%v for v in values)+'},\n'
 text+='};\n'
 for key,(reg,field) in fields.items():
  values=[]
  for pipe in range(5):
   name=registers[reg].format(i=pipe)+'__'+field.format(i=pipe)
   values.append((f[name+'_MASK'],f[name+'__SHIFT']))
  assert all(v==values[0] for v in values),'Fields differ between physical pipes: '+key
  text+='#define DCN302_'+key+'_MASK 0x%08xu\n#define DCN302_'%values[0][0]+key+'_SHIFT '+str(values[0][1])+'u\n'
 text+='#endif\n'
 target=root/'tools/gpu-driver/amd/dcn302_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target,len(text),'bytes')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
