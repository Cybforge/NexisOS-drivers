#!/usr/bin/env python3
"""Pin native Navi23 GPIO/DDC/HPD routing; preserve AMD source license."""
from pathlib import Path
import argparse,hashlib
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
def generate(directory):
 for name,expected in expected_sources.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:raise ValueError('Not pinned: '+name)
 source=(directory/'dcn302_offset.h').read_text();offset=definitions(source)
 mask=definitions((directory/'dcn302_mask.h').read_text());bases=definitions((directory/'dimgrey_cavefish_ip_offset.h').read_text())
 def reg(name):
  n='mm'+name;v=(bases['DCN_BASE__INST0_SEG'+str(offset[n+'_BASE_IDX'])]+offset[n])*4
  assert 0<=v<1024*1024 and not v&3;return v
 text=source[:source.index('*/')+2]+'\n/* Generated checksum-pinned AMD v6.12 DCN302 native routing. */\n'
 text+='#ifndef NEXIS_DCN302_ROUTE_REGS_H\n#define NEXIS_DCN302_ROUTE_REGS_H\n#include <stdint.h>\n'
 text+='#define DCN302_GPIO_HPD_BYTES 0x%05xu\n'%reg('DC_GPIO_HPD_A')
 # The common GPIO word names HPD6, but Navi23 has only HPD0..4 engines.
 text+='static const uint32_t dcn302_hpd_mask[5]={'+','.join('0x%08xu'%mask[f'DC_GPIO_HPD_A__DC_GPIO_HPD{i+1}_A_MASK'] for i in range(5))+'};\n'
 text+='static const uint32_t dcn302_hpd_status_bytes[5]={'+','.join('0x%05xu'%reg(f'HPD{i}_DC_HPD_INT_STATUS') for i in range(5))+'};\n'
 for key,field in [('SENSE','DC_HPD_SENSE'),('DELAYED','DC_HPD_SENSE_DELAYED')]:
  v=[mask[f'HPD{i}_DC_HPD_INT_STATUS__{field}_MASK'] for i in range(5)];s=[mask[f'HPD{i}_DC_HPD_INT_STATUS__{field}__SHIFT'] for i in range(5)]
  assert all(n==v[0] for n in v) and all(n==s[0] for n in s)
  text+=f'#define DCN302_HPD_{key}_MASK 0x{v[0]:08x}u\n#define DCN302_HPD_{key}_SHIFT {s[0]}u\n'
 text+='static const uint32_t dcn302_ddc_gpio_bytes[5]={'+','.join('0x%05xu'%reg(f'DC_GPIO_DDC{i+1}_A') for i in range(5))+'};\n'
 text+='static const uint32_t dcn302_ddc_clk_mask[5]={'+','.join('0x%08xu'%mask[f'DC_GPIO_DDC{i+1}_A__DC_GPIO_DDC{i+1}CLK_A_MASK'] for i in range(5))+'};\n'
 text+='#endif\n';target=root/'tools/gpu-driver/amd/dcn302_route_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
