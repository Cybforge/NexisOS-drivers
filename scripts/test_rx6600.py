#!/usr/bin/env python3
"""Exercise the actual retained RX6600 backend; never install its pending binary."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess
from build_gpu_module_v2 import build
root=Path(__file__).resolve().parents[1];out=root/'build/rx6600-tests';out.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sanitizer',action='store_true');args=p.parse_args()
native=['tools/gpu-driver/amd/rx6600_module.c','tools/gpu-driver/amd/rx6600.c',
 'tools/gpu-driver/amd/atom_tables.c','tools/gpu-driver/amd/atom_board.c','tools/gpu-driver/amd/dcn302_route.c',
 'tools/gpu-driver/amd/dcn302_surface.c','tools/gpu-driver/amd/dcn302_otg.c','tools/gpu-driver/amd/dcn302_clock.c',
 'tools/gpu-driver/amd/dcn302_smu.c','tools/gpu-driver/amd/dcn302_dfs.c','tools/gpu-driver/amd/atom_memory.c',
 'tools/gpu-driver/amd/dcn302_dml.c','tools/gpu-driver/amd/dcn302_hubp.c','tools/gpu-driver/amd/dcn302_hubbub.c','tools/gpu-driver/amd/dcn302_timing.c','tools/gpu-driver/amd/dml/nexis_dml_port.c',
 'tools/gpu-driver/amd/dml/display_mode_vba.c','tools/gpu-driver/amd/dml/display_mode_vba_30.c',
 'tools/gpu-driver/amd/dml/display_rq_dlg_calc_30.c','tools/gpu-driver/amd/dml/dcn_calc_math.c',
 'tools/gpu-driver/amd/dml/display_rq_dlg_helpers.c']
tracked=['tests/host/test_rx6600.c','kernel/drivers/gpu/module_image.c',*native,
 *[file.relative_to(root).as_posix() for file in (root/'tools/gpu-driver/amd').rglob('*.h')],
 'tools/gpu-driver/amd/dml/upstream-sources.json','tools/gpu-driver/common/memory.c',
 'tools/gpu-driver/include/nexis_gpu_v2.h','scripts/build_gpu_module_v2.py','scripts/test_rx6600.py']
# Bind evidence to inputs read before compilation; reject concurrent changes.
initial_sha={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in tracked}
artifact=out/'candidate.ndrv';metadata=build([root/s for s in native+['tools/gpu-driver/common/memory.c']],artifact,0x1002,0x73ff)
elf=artifact.with_suffix('.elf').read_bytes();at=struct.unpack_from('<Q',elf,40)[0];entry_bytes,count=struct.unpack_from('<HH',elf,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',elf,at+n*entry_bytes) for n in range(count)];symbols={}
for section in sections:
 if section[1]!=2:continue
 text=sections[section[6]];names=elf[text[4]:text[4]+text[5]]
 for pos in range(section[4],section[4]+section[5],section[9]):
  name,flags,other,index,value,size=struct.unpack_from('<IBBHQQ',elf,pos)
  if index:symbols[names[name:].split(b'\0',1)[0].decode()]={'address':value,'bytes':size}
# Zig's freestanding LTO inlines the floor setter into this actual retained
# operation. Verify that real body, not an absent pre-LTO symbol name.
floor=symbols['clock_floor'];assert floor['bytes']>100 and floor['address']+floor['bytes']<=struct.unpack_from('<I',artifact.read_bytes(),16)[0]
dfs=symbols['display_clocks'];assert dfs['bytes']>100 and dfs['address']+dfs['bytes']<=struct.unpack_from('<I',artifact.read_bytes(),16)[0]
bandwidth=symbols['bandwidth_plan'];assert bandwidth['bytes']>100 and bandwidth['address']+bandwidth['bytes']<=struct.unpack_from('<I',artifact.read_bytes(),16)[0]
hubp=symbols['bandwidth_registers'];assert hubp['bytes']>100 and hubp['address']+hubp['bytes']<=struct.unpack_from('<I',artifact.read_bytes(),16)[0]
timing=symbols['timing_registers'];assert timing['bytes']>100 and timing['address']+timing['bytes']<=struct.unpack_from('<I',artifact.read_bytes(),16)[0]
sources=['tests/host/test_rx6600.c','kernel/drivers/gpu/module_image.c']+native
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe';exe=out/'rx6600.exe'
warnings=['-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-pointer-sign']
subprocess.run([gcc,'-std=c11','-O2',*warnings,*[str(root/s) for s in sources],'-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(artifact),str(floor['address']),str(dfs['address']),str(bandwidth['address']),str(hubp['address']),str(timing['address'])],capture_output=True,text=True)
if r.returncode:raise SystemExit(r.stderr or r.stdout)
report=json.loads(r.stdout)
if args.sanitizer:
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe');exe=out/'rx6600_ubsan.exe'
 subprocess.run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O1',*warnings,'-fsanitize=undefined','-fno-sanitize-recover=all',*[str(root/s) for s in sources],'-o',str(exe)],check=True)
 r=subprocess.run([str(exe),str(artifact),str(floor['address']),str(dfs['address']),str(bandwidth['address']),str(hubp['address']),str(timing['address'])],capture_output=True,text=True,check=True);assert json.loads(r.stdout)==report
 report['undefined_behavior_sanitizer_passed']=True
report.update(candidate=metadata,distributed=False,full_rx6600_driver_complete=False,physical_gpu_drivers_complete=0,
 real_pic_bandwidth_registers_executed=True,native_hubp_writes_and_reverse_rollback_modeled=True,
 native_hubp_clocks_and_floor_binding_verified=True,native_hubbub_watermarks_and_policy_integrated=True,
 native_hubbub_reference_used_for_dml=True,native_uclk_change_rejected_while_pstate_forced=True,
 real_pic_timing_registers_executed=True,native_global_sync_bound_to_same_dml=True,
 native_timing_parent_guard_verified=True,native_timing_restore_before_fetch_policy=True,
 native_timing_apply_and_restore_write_faults_checked=True,native_timing_lost_state_quarantine_verified=True)
report['native_pic_floor_command_symbol']=floor
report['native_pic_display_clock_command_symbol']=dfs
report['native_pic_bandwidth_plan_symbol']=bandwidth
report['native_pic_bandwidth_registers_symbol']=hubp
report['native_pic_timing_registers_symbol']=timing
assert all(hashlib.sha256((root/s).read_bytes()).hexdigest()==sha for s,sha in initial_sha.items()),'Inputs changed during compilation/tests'
report['source_sha256']=initial_sha
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
