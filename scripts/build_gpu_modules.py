"""Build a pinned, init-only external GPU module without embedding its code in the OS."""
from pathlib import Path
import struct,subprocess,hashlib,json,os,shutil
ROOT=Path(__file__).resolve().parents[1]
def main():
 out=ROOT/'build/gpu-drivers';out.mkdir(exist_ok=True)
 zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
 elf=out/'bochs.elf'
 subprocess.run([str(zig),'cc','-target','x86_64-freestanding','-O2','-Ddriver_init=_start','-fPIE','-pie','-ffreestanding','-mno-red-zone','-fno-stack-protector','-nostdlib','-Wl,-T,'+str(ROOT/'tools/gpu-driver/module.ld'),str(ROOT/'tools/gpu-driver/bochs/bochs.c'),'-o',str(elf)],check=True)
 d=elf.read_bytes();entry=struct.unpack_from('<Q',d,24)[0];off=struct.unpack_from('<Q',d,40)[0];ents,n,strings=struct.unpack_from('<HHH',d,58)
 sections=[struct.unpack_from('<IIQQQQIIQQ',d,off+i*ents) for i in range(n)]
 names=sections[strings];strtab=d[names[4]:names[4]+names[5]];size=0
 for s in sections:
  name=strtab[s[0]:].split(b'\0',1)[0].decode()
  assert not (s[1] in (4,9) and s[5]),'Relocations are forbidden: '+name
  if s[2]&2 and s[1]!=6:
   assert s[5]<=65536 and not (s[2]&1 and s[5]),'Writable global data is forbidden: '+name
   size=max(size,s[3]+s[5])
 assert 0<size<=65536 and entry<size
 code=bytearray(size)
 for s in sections:
  if s[2]&2 and s[1]!=6 and s[5]:code[s[3]:s[3]+s[5]]=d[s[4]:s[4]+s[5]]
 module=b'NDRV'+struct.pack('<III',1,entry,len(code))+code
 (out/'bochs.ndrv').write_bytes(module);digest=hashlib.sha256(module).digest()
 header='#ifndef NEXIS_GPU_PINS_H\n#define NEXIS_GPU_PINS_H\nstatic const unsigned char bochs_sha256[32] = {'+','.join('0x%02x'%b for b in digest)+'};\n#define BOCHS_DRIVER_URL "https://raw.githubusercontent.com/jojojonas169-debug/NexisOS-drivers/main/packages/bochs.ndrv"\n#endif\n'
 (ROOT/'kernel/drivers/gpu/pins.h').write_text(header,newline='\n')
 (out/'catalog.json').write_text(json.dumps({'abi':1,'modules':[{'name':'bochs','pci_vendor':'1234','pci_device':'1111','file':'packages/bochs.ndrv','sha256':digest.hex(),'native_modesetting':True,'retains_desktop_dimensions':True,'refresh_clock_control':False,'hdmi_audio':False,'hardware_rendering':False}],'physical_gpu_drivers':{'rx6600':'not implemented','other_requested_cards':'not implemented'}},indent=2)+'\n')
 print('External Bochs module:',len(module),'bytes, SHA256',digest.hex())
if __name__=='__main__':main()
