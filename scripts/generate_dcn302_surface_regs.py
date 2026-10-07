#!/usr/bin/env python3
"""Extract exact Navi23 MPC/HUBP/VRAM aperture fields from pinned AMD headers."""
from pathlib import Path
import argparse,hashlib
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
registers={
 'OUT_MUX':'MPC_OUT{i}_MUX','TOP':'MPCC{i}_MPCC_TOP_SEL','BOTTOM':'MPCC{i}_MPCC_BOT_SEL','OPP':'MPCC{i}_MPCC_OPP_ID','MODE':'MPCC{i}_MPCC_CONTROL',
 'CONFIG':'HUBP{i}_DCSURF_SURFACE_CONFIG','TILING':'HUBP{i}_DCSURF_TILING_CONFIG','VIEW_START':'HUBP{i}_DCSURF_PRI_VIEWPORT_START','VIEW_SIZE':'HUBP{i}_DCSURF_PRI_VIEWPORT_DIMENSION',
 'HUBP':'HUBP{i}_DCHUBP_CNTL','CLOCK':'HUBP{i}_HUBP_CLK_CNTL','CROSSBAR':'HUBPRET{i}_HUBPRET_CONTROL',
 'PITCH':'HUBPREQ{i}_DCSURF_SURFACE_PITCH','ADDRESS':'HUBPREQ{i}_DCSURF_PRIMARY_SURFACE_ADDRESS','ADDRESS_HIGH':'HUBPREQ{i}_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH',
 'CONTROL':'HUBPREQ{i}_DCSURF_SURFACE_CONTROL','INUSE':'HUBPREQ{i}_DCSURF_SURFACE_EARLIEST_INUSE','INUSE_HIGH':'HUBPREQ{i}_DCSURF_SURFACE_EARLIEST_INUSE_HIGH',
}
fields={
 'MUX':('OUT_MUX','MPC_OUT_MUX'),'TOP':('TOP','MPCC_TOP_SEL'),'BOTTOM':('BOTTOM','MPCC_BOT_SEL'),'OPP':('OPP','MPCC_OPP_ID'),'MODE':('MODE','MPCC_MODE'),
 'FORMAT':('CONFIG','SURFACE_PIXEL_FORMAT'),'ROTATION':('CONFIG','ROTATION_ANGLE'),'MIRROR':('CONFIG','H_MIRROR_EN'),'ALPHA':('CONFIG','ALPHA_PLANE_EN'),
 'SW_MODE':('TILING','SW_MODE'),'X':('VIEW_START','PRI_VIEWPORT_X_START'),'Y':('VIEW_START','PRI_VIEWPORT_Y_START'),
 'WIDTH':('VIEW_SIZE','PRI_VIEWPORT_WIDTH'),'HEIGHT':('VIEW_SIZE','PRI_VIEWPORT_HEIGHT'),
 'BLANK':('HUBP','HUBP_BLANK_EN'),'TTU_DISABLE':('HUBP','HUBP_TTU_DISABLE'),'UNDERFLOW':('HUBP','HUBP_UNDERFLOW_STATUS'),
 'CLOCK_ENABLE':('CLOCK','HUBP_CLOCK_ENABLE'),'DISP_ON':('CLOCK','HUBP_DISPCLK_R_CLOCK_ON'),'DPP_ON':('CLOCK','HUBP_DPPCLK_G_CLOCK_ON'),
 'RED':('CROSSBAR','CROSSBAR_SRC_CR_R'),'GREEN':('CROSSBAR','CROSSBAR_SRC_Y_G'),'BLUE':('CROSSBAR','CROSSBAR_SRC_CB_B'),
 'PITCH':('PITCH','PITCH'),'HIGH':('ADDRESS_HIGH','PRIMARY_SURFACE_ADDRESS_HIGH'),
 'DCC':('CONTROL','PRIMARY_SURFACE_DCC_EN'),'TMZ':('CONTROL','PRIMARY_SURFACE_TMZ'),
 'INUSE_HIGH':('INUSE_HIGH','SURFACE_EARLIEST_INUSE_ADDRESS_HIGH'),'VMID':('INUSE_HIGH','SURFACE_EARLIEST_INUSE_VMID'),
}
def generate(directory):
 for name,expected in expected_sources.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:raise ValueError('Not pinned: '+name)
 source=(directory/'dcn302_offset.h').read_text();d=definitions(source);f=definitions((directory/'dcn302_mask.h').read_text());b=definitions((directory/'dimgrey_cavefish_ip_offset.h').read_text())
 def address(name):
  n='mm'+name;v=(b['DCN_BASE__INST0_SEG'+str(d[n+'_BASE_IDX'])]+d[n])*4
  assert 0<=v<1024*1024 and not v&3;return v
 text=source[:source.index('*/')+2]+'\n/* Generated checksum-pinned AMD v6.12 Navi23 surface registers. */\n'
 text+='#ifndef NEXIS_DCN302_SURFACE_REGS_H\n#define NEXIS_DCN302_SURFACE_REGS_H\n#include <stdint.h>\n'
 text+='enum dcn302_surface_register {\n'+''.join(' DCN302_SURFACE_R_'+n+',\n' for n in registers)+' DCN302_SURFACE_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_surface_register_bytes[5][DCN302_SURFACE_REGISTER_COUNT]={\n'
 for i in range(5):text+=' {'+','.join('0x%05xu'%address(v.format(i=i)) for v in registers.values())+'},\n'
 text+='};\n'
 for name,(reg,field) in fields.items():
  values=[(f[registers[reg].format(i=i)+'__'+field+'_MASK'],f[registers[reg].format(i=i)+'__'+field+'__SHIFT']) for i in range(5)]
  assert all(v==values[0] for v in values),name
  text+=f'#define DCN302_SURFACE_{name}_MASK 0x{values[0][0]:08x}u\n#define DCN302_SURFACE_{name}_SHIFT {values[0][1]}u\n'
 for key,reg,field in [('BASE','DCN_VM_FB_LOCATION_BASE','FB_BASE'),('TOP','DCN_VM_FB_LOCATION_TOP','FB_TOP'),('OFFSET','DCN_VM_FB_OFFSET','FB_OFFSET')]:
  text+=f'#define DCN302_FB_{key}_BYTES 0x{address(reg):05x}u\n#define DCN302_FB_{key}_MASK 0x{f[reg+"__"+field+"_MASK"]:08x}u\n#define DCN302_FB_{key}_SHIFT {f[reg+"__"+field+"__SHIFT"]}u\n'
 text+='#endif\n';target=root/'tools/gpu-driver/amd/dcn302_surface_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
