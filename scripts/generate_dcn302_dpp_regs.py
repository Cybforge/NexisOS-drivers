#!/usr/bin/env python3
"""Native DCN3 float scaler/LB, shared HUBP/DPP cursor and viewport subset."""
from pathlib import Path
import argparse,hashlib,json,re
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
pins={**expected_sources,
 'dcn302_resource.c':'54362248c36a9ee830042b6a220637ff678b56cb6772fd3f43139a9315cca4bc',
 'dcn30_resource.c':'4cbc395956fb8349cfb187a5818b8297ec942d5e1ad481768db4fb20902aaff2',
 'dcn30_dpp.c':'7b27b1c37f2144a8e6fa45edb17c277ab5320fcfed3e931b784bacb7956b4b7a',
 'dcn20_dpp.c':'50a4348765cb83a5ae6c113c21c97447bbe56eb9bd2375d3f993d54b9fc37429',
 'dcn10_dpp_dscl.c':'a0ad94be8d89e0856dbc63c58fd79e5870f05740fcf6153ffe9d34100d86d507',
 'dcn10_dpp_cm.c':'0f008fdefd62ae7d7e59e236d9ecd1771f995cceb08c89601f8cd3a943554488',
 'dcn30_dpp.h':'7cd211a82e9f89adba074d97ecd53c1e237be4965327df9c2adce184c9d21935',
 'dcn20_hubp.h':'4f633a0658972f0e02b6af0833308c35914511f4ccdfa99e206b9c48db98987a'}
registers={
 'CURSOR':'CURSOR0_{i}_CURSOR_CONTROL','CNVC_CURSOR':'CNVC_CUR{i}_CURSOR0_CONTROL',
 'PIXEL_FORMAT':'CNVC_CFG{i}_CNVC_SURFACE_PIXEL_FORMAT','FORMAT':'CNVC_CFG{i}_FORMAT_CONTROL',
 'DEALPHA':'CNVC_CFG{i}_PRE_DEALPHA','REALPHA':'CNVC_CFG{i}_PRE_REALPHA',
 'DEGAM':'CNVC_CFG{i}_PRE_DEGAM','CSC':'CM{i}_CM_POST_CSC_CONTROL',
 'CM':'CM{i}_CM_CONTROL','KEYER':'CNVC_CFG{i}_COLOR_KEYER_CONTROL',
 'AUTOCAL':'DSCL{i}_DSCL_AUTOCAL','BOUNDARY':'DSCL{i}_DSCL_CONTROL',
 'RECOUT_START':'DSCL{i}_RECOUT_START','RECOUT_SIZE':'DSCL{i}_RECOUT_SIZE','MPC_SIZE':'DSCL{i}_MPC_SIZE',
 'LB_FORMAT':'DSCL{i}_LB_DATA_FORMAT','LB_MEMORY':'DSCL{i}_LB_MEMORY_CTRL','MODE':'DSCL{i}_SCL_MODE',
 'VIEW_START':'HUBP{i}_DCSURF_PRI_VIEWPORT_START','VIEW_SIZE':'HUBP{i}_DCSURF_PRI_VIEWPORT_DIMENSION',
 'POWER':'DSCL{i}_DSCL_MEM_PWR_CTRL','STATUS':'DSCL{i}_DSCL_MEM_PWR_STATUS','CLOCK':'HUBP{i}_HUBP_CLK_CNTL',
 'LOCAL_CLOCK':'DPP_TOP{i}_DPP_CONTROL'}
fields={
 'CURSOR_ENABLE':('CURSOR','CURSOR_ENABLE'), 'CNVC_ENABLE':('CNVC_CURSOR','CUR0_ENABLE'),
 'PIXEL_FORMAT':('PIXEL_FORMAT','CNVC_SURFACE_PIXEL_FORMAT'),'ALPHA_PLANE':('PIXEL_FORMAT','CNVC_ALPHA_PLANE_ENABLE'),
 'EXPANSION':('FORMAT','FORMAT_EXPANSION_MODE'),'CNV16':('FORMAT','FORMAT_CNV16'),
 'FORMAT_ALPHA':('FORMAT','ALPHA_EN'),'CNVC_BYPASS':('FORMAT','CNVC_BYPASS'),
 'MSB_ALIGN':('FORMAT','CNVC_BYPASS_MSB_ALIGN'),'CLAMP':('FORMAT','CLAMP_POSITIVE'),'CLAMP_C':('FORMAT','CLAMP_POSITIVE_C'),
 'FORMAT_R':('FORMAT','FORMAT_CROSSBAR_R'),'FORMAT_G':('FORMAT','FORMAT_CROSSBAR_G'),'FORMAT_B':('FORMAT','FORMAT_CROSSBAR_B'),
 'DEALPHA':('DEALPHA','PRE_DEALPHA_EN'),'DEALPHA_BLEND':('DEALPHA','PRE_DEALPHA_ABLND_EN'),
 'REALPHA':('REALPHA','PRE_REALPHA_EN'),'REALPHA_BLEND':('REALPHA','PRE_REALPHA_ABLND_EN'),
 'DEGAM_MODE':('DEGAM','PRE_DEGAM_MODE'),'DEGAM_SELECT':('DEGAM','PRE_DEGAM_SELECT'),
 'CSC_MODE':('CSC','CM_POST_CSC_MODE'),'CM_BYPASS':('CM','CM_BYPASS'),'KEYER':('KEYER','COLOR_KEYER_EN'),
 'AUTOCAL_MODE':('AUTOCAL','AUTOCAL_MODE'),'AUTOCAL_PIPES':('AUTOCAL','AUTOCAL_NUM_PIPE'),
 'AUTOCAL_ID':('AUTOCAL','AUTOCAL_PIPE_ID'),'BOUNDARY':('BOUNDARY','SCL_BOUNDARY_MODE'),
 'RECOUT_X':('RECOUT_START','RECOUT_START_X'),'RECOUT_Y':('RECOUT_START','RECOUT_START_Y'),
 'RECOUT_W':('RECOUT_SIZE','RECOUT_WIDTH'),'RECOUT_H':('RECOUT_SIZE','RECOUT_HEIGHT'),
 'MPC_W':('MPC_SIZE','MPC_WIDTH'),'MPC_H':('MPC_SIZE','MPC_HEIGHT'),
 'INTERLEAVE':('LB_FORMAT','INTERLEAVE_EN'),'ALPHA':('LB_FORMAT','ALPHA_EN'),
 'LB_CONFIG':('LB_MEMORY','MEMORY_CONFIG'),'LB_MAX':('LB_MEMORY','LB_MAX_PARTITIONS'),
 'MODE':('MODE','DSCL_MODE')}
def function(source,name):
 pattern=r'(?:^|\n)(?:static\s+)?(?:struct\s+\w+|enum\s+\w+|void|bool|int|uint32_t|unsigned)\s*\*?\s*'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{'
 match=re.search(pattern,source);assert match,('missing function definition',name)
 start=match.end()-1;depth=1;end=start+1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def generate(ref):
 for n,sha in pins.items():assert hashlib.sha256((ref/n).read_bytes()).hexdigest()==sha,n
 resource=(ref/'dcn302_resource.c').read_text();dpp=(ref/'dcn30_dpp.c').read_text();manual=(ref/'dcn10_dpp_dscl.c').read_text()
 assert '.populate_dml_pipes = dcn30_populate_dml_pipes_from_context' in resource and 'dpp3_construct' in function(resource,'dcn302_dpp_create')
 assert 'dm_lb_16' in function((ref/'dcn30_resource.c').read_text(),'dcn30_populate_dml_pipes_from_context')
 assert '.dscl_data_proc_format = DSCL_DATA_PRCESSING_FLOAT_FORMAT' in dpp and '.dscl_calc_lb_num_partitions = dscl2_calc_lb_num_partitions' in dpp
 assert '.dpp_set_scaler\t\t\t= dpp1_dscl_set_scaler_manual_scale' in dpp
 assert '.dpp_setup\t\t\t= dpp3_cnv_setup' in dpp and '.dpp_full_bypass\t\t= dpp1_full_bypass' in dpp
 setup=function(dpp,'dpp3_cnv_setup')
 for value in ['FORMAT_CROSSBAR_R, 0','FORMAT_CROSSBAR_G, 1','FORMAT_CROSSBAR_B, 2','pixel_format = 8;']:
  assert value in setup,value
 assert 'CM_POST_CSC_MODE, 0' in function(dpp,'dpp3_program_post_csc')
 assert 'pre_degam_en = 0; //bypass' in function(dpp,'dpp3_set_pre_degam')
 assert 'CM_BYPASS, 1' in function((ref/'dcn10_dpp_cm.c').read_text(),'dpp1_full_bypass')
 assert 'DSCL_MODE_SCALING_444_BYPASS = 0' in manual and 'LB_MEMORY_CONFIG_0' in function(manual,'dpp1_dscl_find_lb_memory_config')
 for text in [(ref/'dcn30_dpp.h').read_text(),(ref/'dcn20_hubp.h').read_text()]:assert 'SRI(CURSOR_CONTROL, CURSOR0_, id)' in text
 assert 'SRI(CURSOR0_CONTROL, CNVC_CUR, id)' in (ref/'dcn30_dpp.h').read_text()
 src=(ref/'dcn302_offset.h').read_text();offsets=definitions(src);masks=definitions((ref/'dcn302_mask.h').read_text());bases=definitions((ref/'dimgrey_cavefish_ip_offset.h').read_text())
 names=list(registers);owned=[0]*len(names);ro=[0]*len(names);macros={}
 def field(reg,name):
  values=[]
  for p in range(5):
   prefix=registers[reg].format(i=p)+'__'+name;values.append((masks[prefix+'_MASK'],masks[prefix+'__SHIFT']))
  assert all(v==values[0] for v in values);return values[0]
 for key,(reg,name) in fields.items():
  mask,shift=field(reg,name);owned[names.index(reg)]|=mask;macros[key]=(mask,shift)
 ro[names.index('CNVC_CURSOR')]=field('CNVC_CURSOR','CUR0_UPDATE_PENDING')[0]
 ro[names.index('FORMAT')]=field('FORMAT','CNVC_UPDATE_PENDING')[0]
 ro[names.index('CSC')]=field('CSC','CM_POST_CSC_MODE_CURRENT')[0]
 ro[names.index('CM')]=field('CM','CM_UPDATE_PENDING')[0]
 ro[names.index('MODE')]=field('MODE','SCL_COEF_RAM_SELECT_CURRENT')[0]
 ro[names.index('LB_MEMORY')]=field('LB_MEMORY','LB_NUM_PARTITIONS')[0]|field('LB_MEMORY','LB_NUM_PARTITIONS_C')[0]
 ro[names.index('STATUS')]=0xffffffff
 lb_power=sum(field('STATUS',f'LB_G{n}_MEM_PWR_STATE')[0] for n in range(1,7))
 clock_on=sum(field('CLOCK',name)[0] for name in ['HUBP_DISPCLK_R_CLOCK_ON','HUBP_DPPCLK_G_CLOCK_ON','HUBP_DCFCLK_R_CLOCK_ON','HUBP_DCFCLK_G_CLOCK_ON'])
 ro[names.index('CLOCK')]=clock_on
 macros.update(LB_POWER=(lb_power,0),CLOCK_REQUIRED=(clock_on|field('CLOCK','HUBP_CLOCK_ENABLE')[0],0))
 for key,reg,native in [('VIEW_X','VIEW_START','PRI_VIEWPORT_X_START'),('VIEW_Y','VIEW_START','PRI_VIEWPORT_Y_START'),
  ('VIEW_W','VIEW_SIZE','PRI_VIEWPORT_WIDTH'),('VIEW_H','VIEW_SIZE','PRI_VIEWPORT_HEIGHT'),
  ('LOCAL_CLOCK_ENABLE','LOCAL_CLOCK','DPP_CLOCK_ENABLE'),('LOCAL_TEST','LOCAL_CLOCK','DPP_TEST_CLK_SEL')]:
  macros[key]=field(reg,native)
 assert not any(a&b for a,b in zip(owned,ro))
 text=src[:src.index('*/')+2]+'\n/* Native DCN3 RGB8 identity scaler and software-cursor ownership. */\n#ifndef NEXIS_DCN302_DPP_REGS_H\n#define NEXIS_DCN302_DPP_REGS_H\n#include <stdint.h>\n'
 text+='enum {\n'+''.join(f' DCN302_DPP_R_{n},\n' for n in names)+' DCN302_DPP_REGISTER_COUNT,\n DCN302_DPP_PROGRAM_COUNT = DCN302_DPP_R_VIEW_START\n};\n'
 text+='static const uint32_t dcn302_dpp_register_bytes[5][DCN302_DPP_REGISTER_COUNT]={\n'
 for p in range(5):
  addresses=[]
  for template in registers.values():
   n='mm'+template.format(i=p);addr=(bases['DCN_BASE__INST0_SEG'+str(offsets[n+'_BASE_IDX'])]+offsets[n])*4
   assert addr<1024*1024 and not addr&3;addresses.append(addr)
  text+=' {'+','.join(f'0x{a:05x}u' for a in addresses)+'},\n'
 text+='};\n'
 for key,values in [('owned',owned),('readonly',ro)]:text+='static const uint32_t dcn302_dpp_'+key+'[]={'+','.join(f'0x{v:08x}u' for v in values)+'};\n'
 for name,(mask,shift) in macros.items():text+=f'#define DCN302_DPP_{name}_MASK 0x{mask:08x}u\n#define DCN302_DPP_{name}_SHIFT {shift}u\n'
 text+='#endif\n';path=root/'tools/gpu-driver/amd/dcn302_dpp_regs.h';path.write_text(text,encoding='utf-8',newline='\n')
 evidence={'source':'AMD Linux v6.12','upstream_sha256':pins,'registers':len(names),'owned_registers':sum(bool(v) for v in owned),'fields':len(fields),
  'native_dcn3_float_line_buffer':True,'native_dcn3_dml_lb16_verified':True,'shared_hubp_dpp_cursor_address_verified':True,
  'native_dcn3_cnv_rgb8_and_color_bypass':True,
  'generated_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 (root/'build/dcn302-dpp-register-sources.json').write_text(json.dumps(evidence,indent=2)+'\n');print(json.dumps(evidence))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
