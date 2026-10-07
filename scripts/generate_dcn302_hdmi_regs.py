#!/usr/bin/env python3
"""Extract Navi23 native HDMI/AFMT registers from pinned AMD v6.12 headers."""
from pathlib import Path
import argparse,hashlib
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
registers={
 'FE':'DIG{i}_DIG_FE_CNTL','BE':'DIG{i}_DIG_BE_CNTL','BE_ENABLE':'DIG{i}_DIG_BE_EN_CNTL',
 'AUDIO_CLOCK':'DIG{i}_AFMT_CNTL','CONTROL':'DIG{i}_HDMI_CONTROL','VBI':'DIG{i}_HDMI_VBI_PACKET_CONTROL',
 'GC':'DIG{i}_HDMI_GC','INFO0':'DIG{i}_HDMI_INFOFRAME_CONTROL0','INFO1':'DIG{i}_HDMI_INFOFRAME_CONTROL1',
 'AUDIO':'DIG{i}_HDMI_AUDIO_PACKET_CONTROL','ACR':'DIG{i}_HDMI_ACR_PACKET_CONTROL',
 'CTS32':'DIG{i}_HDMI_ACR_32_0','N32':'DIG{i}_HDMI_ACR_32_1',
 'CTS44':'DIG{i}_HDMI_ACR_44_0','N44':'DIG{i}_HDMI_ACR_44_1',
 'CTS48':'DIG{i}_HDMI_ACR_48_0','N48':'DIG{i}_HDMI_ACR_48_1',
 'AFMT_PACKET':'AFMT{i}_AFMT_AUDIO_PACKET_CONTROL','AFMT_PACKET2':'AFMT{i}_AFMT_AUDIO_PACKET_CONTROL2',
 'AFMT_SOURCE':'AFMT{i}_AFMT_AUDIO_SRC_CONTROL','AFMT_INFO':'AFMT{i}_AFMT_INFOFRAME_CONTROL0',
 'CS0':'AFMT{i}_AFMT_60958_0','CS1':'AFMT{i}_AFMT_60958_1','CS2':'AFMT{i}_AFMT_60958_2',
 'AFMT_POWER':'AFMT{i}_AFMT_MEM_PWR',
}
fields={
 'PIPE':('FE','DIG_SOURCE_SELECT'),'RGB_ENCODING':('FE','TMDS_PIXEL_ENCODING'),'COLOR_FORMAT':('FE','TMDS_COLOR_FORMAT'),
 'LINK_MODE':('BE','DIG_MODE'),'FE_SOURCE':('BE','DIG_FE_SOURCE_SELECT'),'HPD':('BE','DIG_HPD_SELECT'),
 'LINK_ENABLE':('BE_ENABLE','DIG_ENABLE'),'LINK_CLOCK':('BE_ENABLE','DIG_SYMCLK_BE_ON'),
 'CLOCK_ENABLE':('AUDIO_CLOCK','AFMT_AUDIO_CLOCK_EN'),'CLOCK_ON':('AUDIO_CLOCK','AFMT_AUDIO_CLOCK_ON'),
 'PACKET_VERSION':('CONTROL','HDMI_PACKET_GEN_VERSION'),'KEEPOUT':('CONTROL','HDMI_KEEPOUT_MODE'),
 'DEEP_ENABLE':('CONTROL','HDMI_DEEP_COLOR_ENABLE'),'DEEP_DEPTH':('CONTROL','HDMI_DEEP_COLOR_DEPTH'),
 'SCRAMBLE':('CONTROL','HDMI_DATA_SCRAMBLE_EN'),'CLOCK_RATIO':('CONTROL','HDMI_CLOCK_CHANNEL_RATE'),
 'NO_EXTRA_NULL':('CONTROL','HDMI_NO_EXTRA_NULL_PACKET_FILLED'),
 'GC_CONT':('VBI','HDMI_GC_CONT'),'GC_SEND':('VBI','HDMI_GC_SEND'),'NULL_SEND':('VBI','HDMI_NULL_SEND'),
 'AVMUTE':('GC','HDMI_GC_AVMUTE'),'INFO_SEND':('INFO0','HDMI_AUDIO_INFO_SEND'),'INFO_LINE':('INFO1','HDMI_AUDIO_INFO_LINE'),
 'AUDIO_DELAY':('AUDIO','HDMI_AUDIO_DELAY_EN'),'ACR_SEND':('ACR','HDMI_ACR_AUTO_SEND'),'ACR_SOURCE':('ACR','HDMI_ACR_SOURCE'),
 'ACR_PRIORITY':('ACR','HDMI_ACR_AUDIO_PRIORITY'),'ACR_MULTIPLE':('ACR','HDMI_ACR_N_MULTIPLE'),
 'CTS32':('CTS32','HDMI_ACR_CTS_32'),'N32':('N32','HDMI_ACR_N_32'),
 'CTS44':('CTS44','HDMI_ACR_CTS_44'),'N44':('N44','HDMI_ACR_N_44'),
 'CTS48':('CTS48','HDMI_ACR_CTS_48'),'N48':('N48','HDMI_ACR_N_48'),
 'SAMPLE_SEND':('AFMT_PACKET','AFMT_AUDIO_SAMPLE_SEND'),'CS_UPDATE':('AFMT_PACKET','AFMT_60958_CS_UPDATE'),
 'LAYOUT_OVERRIDE':('AFMT_PACKET2','AFMT_AUDIO_LAYOUT_OVRD'),'OSF_OVERRIDE':('AFMT_PACKET2','AFMT_60958_OSF_OVRD'),
 'CHANNELS':('AFMT_PACKET2','AFMT_AUDIO_CHANNEL_ENABLE'),'SOURCE':('AFMT_SOURCE','AFMT_AUDIO_SRC_SELECT'),
 'INFO_UPDATE':('AFMT_INFO','AFMT_AUDIO_INFO_UPDATE'),
 'CHANNEL_L':('CS0','AFMT_60958_CS_CHANNEL_NUMBER_L'),'CLOCK_ACCURACY':('CS0','AFMT_60958_CS_CLOCK_ACCURACY'),
 'CHANNEL_R':('CS1','AFMT_60958_CS_CHANNEL_NUMBER_R'),
 **{'CHANNEL'+str(n):('CS2','AFMT_60958_CS_CHANNEL_NUMBER_'+str(n)) for n in range(2,8)},
 'POWER_FORCE':('AFMT_POWER','AFMT_MEM_PWR_FORCE'),'POWER_STATE':('AFMT_POWER','AFMT_MEM_PWR_STATE'),
}
def generate(directory):
 for name,expected in expected_sources.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:raise ValueError('Not pinned: '+name)
 offset=(directory/'dcn302_offset.h').read_text();d=definitions(offset)
 f=definitions((directory/'dcn302_mask.h').read_text());base=definitions((directory/'dimgrey_cavefish_ip_offset.h').read_text())
 text=offset[:offset.index('*/')+2]+'\n/* Generated from checksum-pinned AMD Linux v6.12 DCN302 headers. */\n'
 text+='#ifndef NEXIS_DCN302_HDMI_REGS_H\n#define NEXIS_DCN302_HDMI_REGS_H\n#include <stdint.h>\n'
 text+='enum dcn302_hdmi_register {\n'+''.join(' DCN302_HDMI_R_'+n+',\n' for n in registers)+' DCN302_HDMI_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_hdmi_register_bytes[5][DCN302_HDMI_REGISTER_COUNT]={\n'
 for inst in range(5):
  a=[]
  for template in registers.values():
   name='mm'+template.format(i=inst);v=(base['DCN_BASE__INST0_SEG'+str(d[name+'_BASE_IDX'])]+d[name])*4
   assert 0<=v<1024*1024 and not v&3;a.append(v)
  text+=' {'+','.join('0x%05xu'%v for v in a)+'},\n'
 text+='};\n'
 # ACP_SEND is defined on DIG0 only in this ASIC's generated register map.
 # Do not write the corresponding reserved bit on DIG1..DIG4.
 text+='static const uint32_t dcn302_hdmi_acp_mask[5]={'+','.join('0x%08xu'%f.get(f'DIG{i}_HDMI_VBI_PACKET_CONTROL__HDMI_ACP_SEND_MASK',0) for i in range(5))+'};\n'
 for key,(reg,field) in fields.items():
  values=[]
  for inst in range(5):
   name=registers[reg].format(i=inst)+'__'+field;values.append((f[name+'_MASK'],f[name+'__SHIFT']))
  assert all(v==values[0] for v in values),key
  text+='#define DCN302_HDMI_'+key+'_MASK 0x%08xu\n#define DCN302_HDMI_'%values[0][0]+key+'_SHIFT '+str(values[0][1])+'u\n'
 text+='#endif\n';target=root/'tools/gpu-driver/amd/dcn302_hdmi_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
