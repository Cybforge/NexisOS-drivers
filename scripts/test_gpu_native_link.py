#!/usr/bin/env python3
"""Link real native AMD code into an external PIC fixture; never distribute it."""
from pathlib import Path
import hashlib,json,struct
from build_gpu_module_v2 import build
root=Path(__file__).resolve().parents[1];out=root/'build/dcn302-tests'
sources=['tests/host/gpu_native_link_fixture.c','tools/gpu-driver/amd/dcn302_otg.c','tools/gpu-driver/amd/dcn302_clock.c',
 'tools/gpu-driver/amd/dcn302_ddc.c','tools/gpu-driver/amd/dcn302_hdmi.c','tools/gpu-driver/amd/hdmi_scdc.c',
 'tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_vm.c','tools/gpu-driver/common/memory.c',
 'tools/gpu-driver/amd/atom_board.c','tools/gpu-driver/amd/atom_display_commands.c',
 'tools/gpu-driver/amd/dcn302_route.c','tools/gpu-driver/amd/dcn302_surface.c']
artifact=out/'link-fixture.ndrv';info=build([root/s for s in sources],artifact,0x1002,0x73ff)
d=artifact.with_suffix('.elf').read_bytes();off=struct.unpack_from('<Q',d,40)[0];ents,n,strings=struct.unpack_from('<HHH',d,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',d,off+i*ents) for i in range(n)];symbols={}
for section in sections:
 if section[1]!=2:continue
 t=sections[section[6]];names=d[t[4]:t[4]+t[5]]
 for at in range(section[4],section[4]+section[5],section[9]):
  name,flags,other,index,value,size=struct.unpack_from('<IBBHQQ',d,at)
  if index:symbols[names[name:].split(b'\0',1)[0].decode()]={'address':value,'bytes':size}
required=['dcn302_clock_measure','dcn302_otg_program_disabled','dcn302_otg_enable','dcn302_otg_disable',
 'dcn302_ddc_transfer','dcn302_hdmi_prepare','dcn302_hdmi_commit','dcn302_hdmi_restore',
 'hdmi_scdc_configure','hdmi_scdc_verify_link','atom_vm_execute','atom_vm_init',
 'atom_board_open','atom_display_execute','dcn302_route_find','dcn302_surface_bind',
 'atom_hdmi_pixel_parameters','atom_hdmi_stream_parameters','atom_hdmi_transmitter_parameters',
 'atom_hdmi_transmitter_v7_parameters','dcn302_route_connected']
payload=artifact.read_bytes();text_bytes=struct.unpack_from('<I',payload,16)[0]
for label in required:
 assert label in symbols and symbols[label]['bytes']>40 and symbols[label]['address']+symbols[label]['bytes']<=text_bytes,label
assert info['memory_bytes']>=160000 and text_bytes>20000
report={'passed':True,'freestanding_pic_link':True,'imports':False,'runtime_relocations':False,
 'native_code_symbols':{label:symbols[label] for label in required},'fixture':info,
 'distributed':False,'physical_card_driver':False,'physical_hardware_verified':False,
 'source_sha256':{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources+['scripts/build_gpu_module_v2.py','tools/gpu-driver/include/string.h','scripts/test_gpu_native_link.py']}}
(out/'link-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
