#!/usr/bin/env python3
"""Extract Navi23 display DFS/DTO fields from checksum-pinned AMD headers."""
from pathlib import Path
import argparse,hashlib
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
def generate(directory):
 pins={**expected_sources,'dcn30_clk_mgr.h':'b12b3e81171be9092bdc4216aa316a5facda16fb01b200f3f7193a2655ee4456'}
 for name,expected in pins.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:raise ValueError('Not pinned: '+name)
 source=(directory/'dcn302_offset.h').read_text();d=definitions(source);m=definitions((directory/'dcn302_mask.h').read_text());b=definitions((directory/'dimgrey_cavefish_ip_offset.h').read_text());c=definitions((directory/'dcn30_clk_mgr.h').read_text())
 def addr(name):
  n='mm'+name;v=(b['DCN_BASE__INST0_SEG'+str(d[n+'_BASE_IDX'])]+d[n])*4
  assert v<1024*1024 and not v&3;return v
 text=source[:source.index('*/')+2]+'\n/* Generated Linux v6.12 Navi23 display DFS/DTO registers. */\n'
 text+='#ifndef NEXIS_DCN302_DFS_REGS_H\n#define NEXIS_DCN302_DFS_REGS_H\n#include <stdint.h>\n'
 text+=f'#define DCN302_DFS_PLL_BYTES 0x{c["mmCLK02_CLK0_CLK_PLL_REQ"]*4:05x}u\n'
 for key,name in [('DENTIST','DENTIST_DISPCLK_CNTL'),('DTO_CTRL','DPPCLK_DTO_CTRL')]:text+=f'#define DCN302_DFS_{key}_BYTES 0x{addr(name):05x}u\n'
 text+='static const uint32_t dcn302_dfs_dto_bytes[5]={'+','.join(f'0x{addr("DPPCLK"+str(i)+"_DTO_PARAM"):05x}u' for i in range(5))+'};\n'
 fields={'DISP_WRITE':'DENTIST_DISPCLK_WDIVIDER','DISP_READ':'DENTIST_DISPCLK_RDIVIDER','MODE':'DENTIST_DISPCLK_CHG_MODE',
         'DISP_DONE':'DENTIST_DISPCLK_CHG_DONE','DPP_DONE':'DENTIST_DPPCLK_CHG_DONE','DPP_WRITE':'DENTIST_DPPCLK_WDIVIDER',
         'DISP_CHANGE_TOGGLE':'DENTIST_DISPCLK_CHGTOG','DISP_DONE_TOGGLE':'DENTIST_DISPCLK_DONETOG',
         'DPP_CHANGE_TOGGLE':'DENTIST_DPPCLK_CHGTOG','DPP_DONE_TOGGLE':'DENTIST_DPPCLK_DONETOG'}
 for key,field in fields.items():
  name='DENTIST_DISPCLK_CNTL__'+field
  text+=f'#define DCN302_DFS_{key}_MASK 0x{m[name+"_MASK"]:08x}u\n#define DCN302_DFS_{key}_SHIFT {m[name+"__SHIFT"]}u\n'
 for key,field in [('PHASE','DPPCLK0_DTO_PHASE'),('MODULO','DPPCLK0_DTO_MODULO')]:
  name='DPPCLK0_DTO_PARAM__'+field
  text+=f'#define DCN302_DFS_{key}_MASK 0x{m[name+"_MASK"]:08x}u\n#define DCN302_DFS_{key}_SHIFT {m[name+"__SHIFT"]}u\n'
 for key,field in [('INTEGER','FbMult_int'),('FRACTION','FbMult_frac')]:
  name='CLK3_0_CLK3_CLK_PLL_REQ__'+field;text+=f'#define DCN302_DFS_{key}_MASK 0x{c[name+"_MASK"]:08x}u\n'
 for kind,field in [('enable','ENABLE'),('db','DB_EN')]:
  text+=f'static const uint32_t dcn302_dfs_dto_{kind}[5]={{'+','.join(f'0x{m["DPPCLK_DTO_CTRL__DPPCLK"+str(i)+"_DTO_"+field+"_MASK"]:08x}u' for i in range(5))+'};\n'
 text+='#endif\n';target=root/'tools/gpu-driver/amd/dcn302_dfs_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
