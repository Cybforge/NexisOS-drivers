#!/usr/bin/env python3
"""Exercise native EDID parsing/policy and UEFI output binding, no GPU writes."""
from pathlib import Path
import argparse,ctypes as c,hashlib,json,os,random,shutil,struct,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/edid-tests';out.mkdir(exist_ok=True)
gcc=os.environ.get('NEXIS_HOST_CC') or shutil.which('gcc') or r'C:\Tools\w64devkit\bin\gcc.exe'
common=[gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror']
subprocess.run(common+['tests/host/test_efi_edid.c','-o',str(out/'test_efi_edid.exe')],cwd=root,check=True)
efi=json.loads(subprocess.check_output([str(out/'test_efi_edid.exe')],text=True))
subprocess.run(common+['-shared','kernel/drivers/gpu/edid.c','tests/host/edid_exports.c','-o',str(out/'edid.dll')],cwd=root,check=True)
class Timing(c.Structure):
    _fields_=[(k,c.c_uint32) for k in ['clock_khz','hactive','hsync_start','hsync_end','htotal','vactive','vsync_start','vsync_end','vtotal','flags']]
class Monitor(c.Structure):
    _fields_=[(k,c.c_bool) for k in ['valid','digital','hdmi','scdc','stereo_48k16','incomplete']]+[('product_id',c.c_uint16),('manufacturer',c.c_char*4),('name',c.c_char*14),('max_tmds_khz',c.c_uint32),('count',c.c_uint32),('modes',Timing*128)]
class Limits(c.Structure):
    _fields_=[('max_pixel_khz',c.c_uint32),('max_tmds_khz',c.c_uint32),('hdmi',c.c_bool),('scdc',c.c_bool)]
lib=c.CDLL(str(out/'edid.dll'))
lib.edid_parse.argtypes=[c.POINTER(c.c_uint8),c.c_size_t,c.POINTER(Monitor)];lib.edid_parse.restype=c.c_bool
lib.edid_refresh_millihz.argtypes=[c.POINTER(Timing)];lib.edid_refresh_millihz.restype=c.c_uint32
lib.edid_select_rgb8.argtypes=[c.POINTER(Monitor),c.c_uint32,c.c_uint32,c.POINTER(Limits)];lib.edid_select_rgb8.restype=c.POINTER(Timing)
assert lib.test_edid_monitor_size()==c.sizeof(Monitor) and lib.test_edid_timing_size()==c.sizeof(Timing)
cases=0
def parse(data,expected=True):
    global cases
    m=Monitor();array=(c.c_uint8*max(1,len(data)))(*data)
    ok=lib.edid_parse(array,len(data),c.byref(m));assert ok==expected,(len(data),ok)
    assert m.valid==ok and m.count<=128
    cases+=1;return m
def select(m,width=1920,height=1080,pixel=600000,tmds=600000,hdmi=True,scdc=True):
    limits=Limits(pixel,tmds,hdmi,scdc)
    p=lib.edid_select_rgb8(c.byref(m),width,height,c.byref(limits))
    return p.contents if p else None
def hz(t):return lib.edid_refresh_millihz(c.byref(t)) if t else 0
def checksum(b):b[-1]=(-sum(b[:-1]))&255;return b
def dtd(clock,width,hblank,hfront,hpulse,height,vblank,vfront,vpulse):
    d=bytearray(18);struct.pack_into('<H',d,0,clock//10)
    d[2]=width&255;d[3]=hblank&255;d[4]=((width>>8)<<4)|(hblank>>8)
    d[5]=height&255;d[6]=vblank&255;d[7]=((height>>8)<<4)|(vblank>>8)
    d[8]=hfront&255;d[9]=hpulse&255;d[10]=((vfront&15)<<4)|(vpulse&15)
    d[11]=((hfront>>8)<<6)|((hpulse>>8)<<4)|((vfront>>4)<<2)|(vpulse>>4);d[17]=0x1e
    return d
def base(timing=None,extensions=0):
    b=bytearray(128);b[:8]=bytes.fromhex('00ffffffffffff00');b[8:10]=bytes.fromhex('0472');b[10]=0x58;b[11]=0x10
    b[18:21]=bytes([1,4,0x80]);b[24]=2;b[38:54]=bytes([1,1])*8
    if timing is not None:b[54:72]=timing
    b[126]=extensions;return checksum(b)
def block(tag,payload):return bytes([(tag<<5)|len(payload)])+bytes(payload)
def cta(blocks,timings=()):
    b=bytearray(128);b[0:4]=bytes([2,3,4+len(blocks),0]);b[4:4+len(blocks)]=blocks
    for i,t in enumerate(timings):b[b[2]+i*18:b[2]+(i+1)*18]=t
    return checksum(b)
normal=dtd(148500,1920,280,88,44,1080,45,4,5)
fast=dtd(558100,1920,160,48,64,1080,38,3,5)
forum=block(3,bytes.fromhex('d85dc401788003'))
legacy=block(3,bytes.fromhex('030c0010003844'))
sound=block(1,[9,7,1])
raw=base(normal,1)+cta(forum+legacy+sound+block(2,[16,63]),[fast])
m=parse(raw);assert m.hdmi and m.scdc and m.stereo_48k16 and m.max_tmds_khz==600000
assert m.manufacturer==b'ACR' and 239900<=hz(select(m))<=240100
assert hz(select(m,scdc=False))==120000 and hz(select(m,pixel=165000))==60000
assert hz(select(m,tmds=165000))==60000 and select(m,pixel=0) is None
assert select(m,width=2560,height=1440) is None
assert hz(select(m,tmds=0,hdmi=False))>239900
for position in [0,7,18,54,127,128,200,255]:
    bad=bytearray(raw);bad[position]^=1;parse(bad,False)
for length in [0,1,64,127,128,129,255,257,2049]:parse(raw[:length] if length<=len(raw) else raw+bytes(length-len(raw)),False)
bad=bytearray(raw);bad[126]=16;checksum(bad[:128]) # modify checksum in-place separately
bad[127]=(-sum(bad[:127]))&255;parse(bad,False)
bad=base(normal,1)+cta(bytes([0x5f,16]));parse(bad,False) # CTA payload extends past DTD boundary
bad=base(normal,1)+cta(block(1,[9,7]));parse(bad,False)
old_cta=cta(b'',[fast]);old_cta[1]=2;checksum(old_cta)
m=parse(base(normal,1)+old_cta);assert 239900<hz(select(m,hdmi=False))<240100
bad=base(normal,1)+cta(block(7,[14,97]));m=parse(bad)
assert select(m,width=3840,height=2160) is None # YCbCr420-only timing cannot be selected for RGB8
both=base(normal,1)+cta(block(7,[14,97])+block(2,[97])+forum);m=parse(both)
assert hz(select(m,width=3840,height=2160))==60000
newvic=base(normal,1)+cta(block(2,[193]));m=parse(newvic)
assert any(t.hactive==5120 for t in m.modes[:m.count]) # extended VIC must not be masked to 65
analog=base(normal);analog[20]=0;checksum(analog);m=parse(analog);assert select(m) is None
# DisplayID type VII clock uses kHz, type I uses 10kHz; both encode value minus one.
def displayid(tag,clock):
    b=bytearray(128);b[0]=0x70;b[1]=0x20 if tag==0x22 else 0x13;b[2]=23;b[3]=3
    b[5:8]=bytes([tag,0,20]);d=bytearray(20);unit=1 if tag==0x22 else 10
    d[0:3]=(clock//unit-1).to_bytes(3,'little');d[3]=0x80
    for off,value in [(4,1920),(6,160),(8,48),(10,64),(12,1080),(14,38),(16,3),(18,5)]:struct.pack_into('<H',d,off,value-1)
    d[9]|=128;d[17]|=128;b[8:28]=d;b[28]=(-sum(b[1:28]))&255
    return checksum(b)
for tag in [3,0x22]:
    ext=displayid(tag,558100);m=parse(base(normal,1)+ext);assert 239900<hz(select(m,hdmi=False))<240100
    ext[28]^=1;checksum(ext);parse(base(normal,1)+ext,False) # outer checksum alone insufficient
ext=displayid(0x22,558100);ext[7]=21;ext[28]=(-sum(ext[1:28]))&255;checksum(ext);parse(base(normal,1)+ext,False)
rng=random.Random(6600)
for _ in range(3000):
    bad=bytearray(raw);offset=rng.randrange(len(bad));bad[offset]=rng.randrange(256)
    for j in range(0,len(bad),128):bad[j+127]=(-sum(bad[j:j+127]))&255
    array=(c.c_uint8*len(bad))(*bad);m=Monitor();ok=lib.edid_parse(array,len(bad),c.byref(m));assert m.valid==ok and m.count<=128
    if ok:
        t=select(m)
        if t:assert t.hactive==1920 and t.vactive==1080 and 0<t.clock_khz<=600000 and hz(t)>0 and not t.flags&(4|8|32)
    cases+=1
parser=argparse.ArgumentParser();parser.add_argument('--monitor-edid',type=Path);args=parser.parse_args()
actual=None
if args.monitor_edid:
    data=args.monitor_edid.read_bytes();m=parse(data);t=select(m)
    assert t is not None
    actual={'name':m.name.decode(),'width':t.hactive,'height':t.vactive,'refresh_millihz':hz(t),'clock_khz':t.clock_khz,'sink_max_tmds_khz':m.max_tmds_khz,'scdc':m.scdc,'stereo_48k16':m.stereo_48k16,'active_mode_changed':False}
report={'passed':True,'cases':cases,'efi_output_binding':efi,'monitor_fixture':actual,'native_gpu_modesetting_verified':False,
        'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['kernel/drivers/gpu/edid.c','kernel/drivers/gpu/edid.h','kernel/drivers/gpu/edid_timings.h','boot/efi/edid.h']}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
