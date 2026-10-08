#!/usr/bin/env python3
"""Build an external retained PIC module; never embed it or modify the catalog."""
from pathlib import Path
import argparse,hashlib,os,shutil,struct,subprocess
root=Path(__file__).resolve().parents[1]
def build(sources,output,vendor,device,services_bytes=136):
    assert services_bytes in (104,112,136),'Undefined retained service prefix'
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    zig=os.environ.get('NEXIS_ZIG') or shutil.which('zig') or str(Path.home()/'Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe')
    elf=output.with_suffix('.elf')
    subprocess.run([zig,'cc','-target','x86_64-freestanding','-O2','-Ddriver_init_v2=_start','-fPIE','-pie','-ffreestanding',
                    '-mno-red-zone','-fno-stack-protector','-nostdlib','-I'+str(root/'tools/gpu-driver/include'),'-Wl,-T,'+str(root/'tools/gpu-driver/module_v2.ld'),
                    *map(str,sources),'-o',str(elf)],check=True)
    data=elf.read_bytes();entry=struct.unpack_from('<Q',data,24)[0];off=struct.unpack_from('<Q',data,40)[0];ents,n,strings=struct.unpack_from('<HHH',data,58)
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,off+i*ents) for i in range(n)]
    table=sections[strings];names=data[table[4]:table[4]+table[5]];symbols={}
    for section in sections:
        if section[1] not in (2,11):continue
        string=sections[section[6]];text=data[string[4]:string[4]+string[5]]
        for at in range(section[4],section[4]+section[5],section[9]):
            name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',data,at)
            assert index or not name,'Imported symbols are forbidden'
            if index:symbols[text[name:].split(b'\0',1)[0].decode()]=value
    text_end,writable,image_end,memory=[symbols[k] for k in ['_text_end','_data_start','_image_end','_memory_end']]
    image_end=max(image_end,writable)
    assert 0<text_end<=writable<=image_end<=memory<=1024*1024 and not (writable&4095) and not (memory&4095) and entry<text_end
    payload=bytearray(image_end)
    for section in sections:
        name=names[section[0]:].split(b'\0',1)[0].decode()
        assert not (section[1] in (4,9) and section[5]),'Runtime relocations are forbidden: '+name
        if not (section[2]&2) or not section[5]:continue
        assert name in ('.text','.rodata','.data','.bss','.dynamic','.dynsym','.dynstr','.gnu.hash','.hash'), 'Unexpected allocated section: '+name
        if name in ('.dynamic','.dynsym','.dynstr','.gnu.hash','.hash'):continue # No imports/relocations or runtime linker.
        address,size=section[3],section[5]
        if section[1]==8:
            assert name=='.bss' and writable<=address and address+size<=memory
            continue
        assert address+size<=image_end
        if section[2]&1:assert address>=writable
        if section[2]&4:assert address+size<=text_end
        payload[address:address+size]=data[section[4]:section[4]+size]
    header=bytearray(64);header[:4]=b'NDRV'
    struct.pack_into('<6I',header,4,2,entry,len(payload),text_end,writable,memory)
    # Exact declared service prefix: legacy 104 bytes or resource extension 112.
    # Host tests check both layouts and execute both versions as retained PIC.
    struct.pack_into('<HHII',header,32,vendor,device,services_bytes,64)
    module=bytes(header)+payload;output.write_bytes(module)
    return {'bytes':len(module),'memory_bytes':memory,'services_bytes':services_bytes,'sha256':hashlib.sha256(module).hexdigest()}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--vendor',type=lambda x:int(x,16),required=True);p.add_argument('--device',type=lambda x:int(x,16),required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--services-bytes',type=int,choices=(104,112,136),default=136);p.add_argument('sources',type=Path,nargs='+');a=p.parse_args()
    print(build(a.sources,a.output,a.vendor,a.device,a.services_bytes))
