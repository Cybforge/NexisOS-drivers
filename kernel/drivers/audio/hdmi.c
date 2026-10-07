/* MIT licensed original NexisOS stereo codec implementation.
 * AMD HDA command encodings/capabilities are documented by the AMD-specific
 * paths in Linux v6.12 sound/pci/hda/{hda_eld.c,patch_hdmi.c}.
 * This configures codec audio, not display clocks or the GPU display engine.
 */
#include "hdmi.h"
bool hdmi_codec_is_amd(uint32_t id){return id==0x1002aa01u;}
static bool usable(const hdmi_codec_io *io){return io && io->read && io->write;}
bool hdmi_codec_stereo_sink(const hdmi_codec_io *io,unsigned pin,uint32_t id){
 if(!usable(io) || pin>255)return false;
 uint32_t sense=0;
 if(!io->read(io->context,pin,0xf09,0,&sense) || (sense&0xc0000000u)!=0xc0000000u)return false;
 if(hdmi_codec_is_amd(id)){
  /* AMD has no standard byte-addressed ELD buffer. Its pin exposes the
   * speaker/transport mask and a selected CEA audio descriptor instead. */
  uint32_t allocation=0,sad=0;
  /* Stereo speakers need not have an explicit speaker-allocation block. */
  if(!io->read(io->context,pin,0xf70,0,&allocation) || (allocation&0x300u)!=0x100u)return false;
  if(!io->write(io->context,pin,0x776,1u<<3) || !io->read(io->context,pin,0xf76,0,&sad))return false;
  if(((sad>>3)&15)!=1 || !(sad&0x10000u))return false; /* LPCM / 16 bit */
  return (sad&0x04000000u) || ((sad&7)>=1 && (sad&0x400u));
 }
 uint32_t size=0;
 if(!io->read(io->context,pin,0xf2e,8,&size))return false;
 size&=255;if(size<20)return false;
 uint8_t eld[256];
 for(unsigned i=0;i<size;i++){
  uint32_t value=0;if(!io->read(io->context,pin,0xf2f,i,&value) || !(value&0x80000000u))return false;
  eld[i]=(uint8_t)value;
 }
 unsigned version=eld[0]>>3,name=eld[4]&31,count=eld[5]>>4,baseline=4+(unsigned)eld[2]*4;
 if((version!=2 && version!=31) || baseline>size || baseline<20+name+count*3 || ((eld[5]>>2)&3)!=0)return false;
 for(unsigned i=0;i<count;i++){
  const uint8_t *sad=eld+20+name+i*3;
  if(((sad[0]>>3)&15)==1 && (sad[0]&7)>=1 && (sad[1]&4) && (sad[2]&1))return true;
 }
 return false;
}
bool hdmi_codec_program_stereo(const hdmi_codec_io *io,unsigned pin,unsigned converter,uint32_t id,uint32_t revision){
 if(!usable(io) || pin>255 || converter>255)return false;
#define WRITE(n,v,p) do{if(!io->write(io->context,(n),(v),(p)))return false;}while(0)
 WRITE(converter,0x72d,1);WRITE(converter,0x70d,1);WRITE(converter,0x70e,0);
 if(hdmi_codec_is_amd(id)){
  bool single=(revision&0xff00u)>=0x300;
  WRITE(pin,0x771,0); /* stereo channel allocation; AMD constructs the packet */
  WRITE(pin,0x772,0); /* no stale downmix data */
  if(single){
   WRITE(pin,0x789,1);
   for(unsigned slot=0;slot<8;slot++){
    unsigned command=0x777+slot/2+(slot&1)*14;
    WRITE(pin,command,slot<2 ? (slot<<4)|1 : 0);
   }
   WRITE(converter,0x770,180); /* PCM ramp rate specified for revision 3+ */
  }else{
   /* Old AMD codecs map channel pairs; one command enables left and right. */
   for(unsigned pair=0;pair<4;pair++)WRITE(pin,0x777+pair,pair?0:1);
  }
  uint32_t hbr=0;if(!io->read(io->context,pin,0xf7c,0,&hbr))return false;
  if((hbr&0x11)==0x11)WRITE(pin,0x77c,hbr&~0x10u);
 }else{
  for(unsigned slot=0;slot<8;slot++)WRITE(pin,0x734,((slot<2?slot:15)<<4)|slot);
  uint8_t packet[14]={0x84,1,10,0,1,0,0,0,0,0,0,0,0,0};unsigned sum=0;
  for(unsigned i=0;i<14;i++)sum+=packet[i];
  packet[3]=(uint8_t)(0-sum);
  WRITE(pin,0x732,0);WRITE(pin,0x730,0);
  for(unsigned i=0;i<14;i++)WRITE(pin,0x731,packet[i]);
  WRITE(pin,0x730,0);WRITE(pin,0x732,0xc0);
 }
#undef WRITE
 return true;
}
