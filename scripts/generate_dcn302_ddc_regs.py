#!/usr/bin/env python3
"""Extract native Navi23 I2C registers from checksum-pinned AMD v6.12 headers."""
from pathlib import Path
import argparse,hashlib
from generate_dcn302_regs import definitions,expected_sources
root=Path(__file__).resolve().parents[1]
registers={
 'SETUP':'DC_I2C_DDC{i}_SETUP','SPEED':'DC_I2C_DDC{i}_SPEED','HW_STATUS':'DC_I2C_DDC{i}_HW_STATUS',
 'GPIO':'DC_GPIO_DDC{i}_MASK','ARBITRATION':'DC_I2C_ARBITRATION','CONTROL':'DC_I2C_CONTROL',
 'SW_STATUS':'DC_I2C_SW_STATUS','TRANS0':'DC_I2C_TRANSACTION0','TRANS1':'DC_I2C_TRANSACTION1',
 'TRANS2':'DC_I2C_TRANSACTION2','TRANS3':'DC_I2C_TRANSACTION3','DATA':'DC_I2C_DATA',
 'TIME_BASE':'MICROSECOND_TIME_BASE_DIV','POWER':'DIO_MEM_PWR_CTRL','POWER_STATUS':'DIO_MEM_PWR_STATUS',
}
fields={
 'ENABLE':('SETUP','DC_I2C_DDC{i}_ENABLE'), 'LIMIT':('SETUP','DC_I2C_DDC{i}_TIME_LIMIT'),
 'DRIVE_DATA':('SETUP','DC_I2C_DDC{i}_DATA_DRIVE_EN'),'DRIVE_CLOCK':('SETUP','DC_I2C_DDC{i}_CLK_DRIVE_EN'),
 'DRIVE_SELECT':('SETUP','DC_I2C_DDC{i}_DATA_DRIVE_SEL'),'BYTE_DELAY':('SETUP','DC_I2C_DDC{i}_INTRA_BYTE_DELAY'),
 'TRANS_DELAY':('SETUP','DC_I2C_DDC{i}_INTRA_TRANSACTION_DELAY'),'RESET_LENGTH':('SETUP','DC_I2C_DDC{i}_SEND_RESET_LENGTH'),
 'PRESCALE':('SPEED','DC_I2C_DDC{i}_PRESCALE'),'THRESHOLD':('SPEED','DC_I2C_DDC{i}_THRESHOLD'),
 'START_TIMING':('SPEED','DC_I2C_DDC{i}_START_STOP_TIMING_CNTL'),'HW_OWNER':('HW_STATUS','DC_I2C_DDC{i}_HW_STATUS'),
 'HW_REQUEST':('HW_STATUS','DC_I2C_DDC{i}_HW_REQ'),
 'OWNER':('ARBITRATION','DC_I2C_REG_RW_CNTL_STATUS'),'REQUEST':('ARBITRATION','DC_I2C_SW_USE_I2C_REG_REQ'),
 'RELEASE':('ARBITRATION','DC_I2C_SW_DONE_USING_I2C_REG'),'QUEUE':('ARBITRATION','DC_I2C_NO_QUEUED_SW_GO'),
 'FIRMWARE_REQUEST':('ARBITRATION','DC_I2C_DMCU_USE_I2C_REG_REQ'),
 'GO':('CONTROL','DC_I2C_GO'),'RESET':('CONTROL','DC_I2C_SOFT_RESET'),'STATUS_RESET':('CONTROL','DC_I2C_SW_STATUS_RESET'),
 'SEND_RESET':('CONTROL','DC_I2C_SEND_RESET'),'COUNT':('CONTROL','DC_I2C_TRANSACTION_COUNT'),
 'SELECT':('CONTROL','DC_I2C_DDC_SELECT'),'STATUS':('SW_STATUS','DC_I2C_SW_STATUS'),
 'DONE':('SW_STATUS','DC_I2C_SW_DONE'),'ABORTED':('SW_STATUS','DC_I2C_SW_ABORTED'),
 'TIMED_OUT':('SW_STATUS','DC_I2C_SW_TIMEOUT'),'NACK':('SW_STATUS','DC_I2C_SW_STOPPED_ON_NACK'),
 'OVERFLOW':('SW_STATUS','DC_I2C_SW_BUFFER_OVERFLOW'),'NACK0':('SW_STATUS','DC_I2C_SW_NACK0'),
 'NACK1':('SW_STATUS','DC_I2C_SW_NACK1'),'NACK2':('SW_STATUS','DC_I2C_SW_NACK2'),'NACK3':('SW_STATUS','DC_I2C_SW_NACK3'),
 'RW':('TRANS0','DC_I2C_RW0'),'START':('TRANS0','DC_I2C_START0'),'STOP':('TRANS0','DC_I2C_STOP0'),
 'STOP_NACK':('TRANS0','DC_I2C_STOP_ON_NACK0'),'BYTES':('TRANS0','DC_I2C_COUNT0'),
 'DATA_RW':('DATA','DC_I2C_DATA_RW'),'DATA_BYTE':('DATA','DC_I2C_DATA'),'INDEX':('DATA','DC_I2C_INDEX'),
 'INDEX_WRITE':('DATA','DC_I2C_INDEX_WRITE'),'REF_BASE':('TIME_BASE','MICROSECOND_TIME_BASE_DIV'),
 'XTAL_DIV':('TIME_BASE','XTAL_REF_DIV'),'SLEEP_FORCE':('POWER','I2C_LIGHT_SLEEP_FORCE'),
 'SLEEP_STATE':('POWER_STATUS','I2C_MEM_PWR_STATE'),
 'PIN_CLOCK':('GPIO','DC_GPIO_DDC{i}CLK_MASK'),'PIN_DATA':('GPIO','DC_GPIO_DDC{i}DATA_MASK'),'AUX':('GPIO','AUX_PAD{i}_MODE'),
}
def generate(directory):
 for name,expected in expected_sources.items():
  if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:raise ValueError('Not pinned: '+name)
 offset=(directory/'dcn302_offset.h').read_text();d=definitions(offset)
 f=definitions((directory/'dcn302_mask.h').read_text());base=definitions((directory/'dimgrey_cavefish_ip_offset.h').read_text())
 text=offset[:offset.index('*/')+2]+'\n/* Generated from checksum-pinned AMD Linux v6.12 DCN302 headers. */\n'
 text+='#ifndef NEXIS_DCN302_DDC_REGS_H\n#define NEXIS_DCN302_DDC_REGS_H\n#include <stdint.h>\n'
 text+='enum dcn302_ddc_register {\n'+''.join(' DCN302_DDC_R_'+n+',\n' for n in registers)+' DCN302_DDC_REGISTER_COUNT\n};\n'
 text+='static const uint32_t dcn302_ddc_register_bytes[5][DCN302_DDC_REGISTER_COUNT]={\n'
 for bus in range(1,6):
  a=[]
  for template in registers.values():
   name='mm'+template.format(i=bus);v=(base['DCN_BASE__INST0_SEG'+str(d[name+'_BASE_IDX'])]+d[name])*4
   assert 0<=v<1024*1024 and not v&3;a.append(v)
  text+=' {'+','.join('0x%05xu'%v for v in a)+'},\n'
 text+='};\n'
 for key,(reg,field) in fields.items():
  values=[]
  for bus in range(1,6):
   name=registers[reg].format(i=bus)+'__'+field.format(i=bus);values.append((f[name+'_MASK'],f[name+'__SHIFT']))
  assert all(v==values[0] for v in values),key
  text+='#define DCN302_DDC_'+key+'_MASK 0x%08xu\n#define DCN302_DDC_'%values[0][0]+key+'_SHIFT '+str(values[0][1])+'u\n'
 # The native transaction slots must have the same format, not assumed strides.
 for slot in range(4):
  for suffix in ['RW','START','STOP','STOP_ON_NACK','COUNT']:
   assert f[f'DC_I2C_TRANSACTION{slot}__DC_I2C_{suffix}{slot}_MASK']==f[f'DC_I2C_TRANSACTION0__DC_I2C_{suffix}0_MASK']
   assert f[f'DC_I2C_TRANSACTION{slot}__DC_I2C_{suffix}{slot}__SHIFT']==f[f'DC_I2C_TRANSACTION0__DC_I2C_{suffix}0__SHIFT']
 text+='#endif\n';target=root/'tools/gpu-driver/amd/dcn302_ddc_regs.h';target.write_text(text,encoding='utf-8',newline='\n');print('Generated',target)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);generate(p.parse_args().reference)
