/* Original MIT licensed EDID decoding and RGB8 mode policy. Numeric timing
 * tables retain their upstream MIT notice in edid_timings.h. */
#include "edid.h"
#include <string.h>
#include "edid_timings.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static unsigned u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static bool checksum(const uint8_t *p,unsigned n){unsigned sum=0;for(unsigned i=0;i<n;i++)sum+=p[i];return !(sum&255);}
static bool timing_valid(const edid_timing *t){
    return t && t->clock_khz && t->clock_khz<=4000000 && t->hactive>=64 && t->vactive>=64 &&
        t->hactive<t->hsync_start && t->hsync_start<t->hsync_end && t->hsync_end<=t->htotal && t->htotal<=65536 &&
        t->vactive<t->vsync_start && t->vsync_start<t->vsync_end && t->vsync_end<=t->vtotal && t->vtotal<=65536;
}
uint32_t edid_refresh_millihz(const edid_timing *t){
    if(!timing_valid(t))return 0;
    uint64_t pixels=(uint64_t)t->htotal*t->vtotal;
    return (uint32_t)(((uint64_t)t->clock_khz*1000000+pixels/2)/pixels);
}
static void add(edid_monitor *m,edid_timing t){
    if(!timing_valid(&t)){m->incomplete=true;return;}
    for(unsigned i=0;i<m->count;i++){
        edid_timing *old=&m->modes[i];
        if(old->clock_khz==t.clock_khz && old->hactive==t.hactive && old->vactive==t.vactive &&
           old->htotal==t.htotal && old->vtotal==t.vtotal && old->hsync_start==t.hsync_start &&
           old->hsync_end==t.hsync_end && old->vsync_start==t.vsync_start && old->vsync_end==t.vsync_end &&
           (old->flags&15)==(t.flags&15)){
            old->flags|=t.flags&EDID_PREFERRED;
            if(!(t.flags&EDID_Y420_ONLY))old->flags&=~EDID_Y420_ONLY;
            return;
        }
    }
    if(m->count==NEXIS_EDID_MAX_MODES){m->incomplete=true;return;}
    m->modes[m->count++]=t;
}
static void dtd(edid_monitor *m,const uint8_t *d,bool preferred){
    if(!u16(d))return; /* monitor description/range descriptor */
    if((d[17]&0x18)!=0x18){m->incomplete=true;return;} /* separate digital sync only */
    unsigned ha=d[2]|(d[4]&0xf0)<<4,hb=d[3]|(d[4]&15)<<8;
    unsigned va=d[5]|(d[7]&0xf0)<<4,vb=d[6]|(d[7]&15)<<8;
    unsigned hf=d[8]|(d[11]&0xc0)<<2,hw=d[9]|(d[11]&0x30)<<4;
    unsigned vf=(d[10]>>4)|(d[11]&12)<<2,vw=(d[10]&15)|(d[11]&3)<<4;
    edid_timing t={u16(d)*10,ha,ha+hf,ha+hf+hw,ha+hb,va,va+vf,va+vf+vw,va+vb,
        ((d[17]&2)?EDID_HPOS:0)|((d[17]&4)?EDID_VPOS:0)|((d[17]&0x80)?EDID_INTERLACE:0)|(preferred?EDID_PREFERRED:0)};
    add(m,t);
}
static void dmt(edid_monitor *m,unsigned width,unsigned height,unsigned hz){
    const edid_timing *best=NULL;
    for(unsigned i=0;i<COUNT(drm_dmt_modes);i++){
        const edid_timing *t=&drm_dmt_modes[i];
        if(t->hactive==width && t->vactive==height && !(t->flags&EDID_INTERLACE) &&
           (edid_refresh_millihz(t)+500)/1000==hz && (!best || t->clock_khz<best->clock_khz))best=t;
    }
    if(best)add(m,*best);else m->incomplete=true;
}
static void vic(edid_monitor *m,unsigned value,bool only420){
    unsigned v=value>=129 && value<=192?value&127:value;
    const edid_timing *t=v>=1 && v<=127?&edid_cea_modes_1[v-1]:v>=193 && v<=219?&edid_cea_modes_193[v-193]:NULL;
    if(!t){m->incomplete=true;return;}
    edid_timing timing=*t;
    if(value>=129 && value<=192)timing.flags|=EDID_PREFERRED;
    if(only420)timing.flags|=EDID_Y420_ONLY;
    add(m,timing);
}
static bool cta_blocks(edid_monitor *m,const uint8_t *b,unsigned start,unsigned end){
    for(unsigned off=start;off<end;){
        unsigned tag=b[off]>>5,n=b[off]&31;const uint8_t *p=b+off+1;
        if(off+1+n>end)return false;
        if(tag==1){
            if(n%3)return false;
            for(unsigned s=0;s<n;s+=3)if(((p[s]>>3)&15)==1 && (p[s]&7)>=1 && (p[s+1]&4) && (p[s+2]&1))m->stereo_48k16=true;
        }else if(tag==2){for(unsigned s=0;s<n;s++)vic(m,p[s],false);}
        else if(tag==3 && n>=3){
            unsigned oui=p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16;
            if(oui==0x000c03 && n>=5){m->hdmi=true;if(n>=7 && p[6] && (unsigned)p[6]*5000>m->max_tmds_khz)m->max_tmds_khz=(unsigned)p[6]*5000;}
            if(oui==0xc45dd8 && n>=7){m->hdmi=true;m->scdc|=(p[5]&0x80)!=0;if(p[4] && (unsigned)p[4]*5000>m->max_tmds_khz)m->max_tmds_khz=(unsigned)p[4]*5000;}
        }else if(tag==7 && n && p[0]==14){for(unsigned s=1;s<n;s++)vic(m,p[s],true);}
        off+=1+n;
    }
    return true;
}
static bool displayid(edid_monitor *m,const uint8_t *b){
    unsigned end=5+b[2];
    if(end>=127 || !checksum(b+1,end))return false; /* independent DisplayID section checksum */
    if(b[1]!=0x12 && b[1]!=0x13 && b[1]!=0x20){m->incomplete=true;return true;}
    for(unsigned off=5;off<end;){
        if(end-off<3)return false;
        unsigned tag=b[off],n=b[off+2];const uint8_t *p=b+off+3;
        if(off+3+n>end)return false;
        if(tag==3 || tag==0x22){
            if(n%20)return false;
            for(unsigned a=0;a<n;a+=20){
                const uint8_t *d=p+a;
                unsigned clock=(d[0]|(unsigned)d[1]<<8|(unsigned)d[2]<<16)+1;
                unsigned ha=u16(d+4)+1,hb=u16(d+6)+1,hf=(u16(d+8)&0x7fff)+1,hw=u16(d+10)+1;
                unsigned va=u16(d+12)+1,vb=u16(d+14)+1,vf=(u16(d+16)&0x7fff)+1,vw=u16(d+18)+1;
                edid_timing t={clock*(tag==3?10:1),ha,ha+hf,ha+hf+hw,ha+hb,va,va+vf,va+vf+vw,va+vb,
                    ((d[9]&128)?EDID_HPOS:0)|((d[17]&128)?EDID_VPOS:0)|((d[3]&16)?EDID_INTERLACE:0)|((d[3]&128)?EDID_PREFERRED:0)};
                add(m,t);
            }
        }else if(tag==0x81){if(!cta_blocks(m,p,0,n))return false;}
        else m->incomplete=true;
        off+=3+n;
    }
    return true;
}
bool edid_parse(const uint8_t *b,size_t size,edid_monitor *m){
    static const uint8_t header[8]={0,255,255,255,255,255,255,0};
    if(!m)return false;
    memset(m,0,sizeof(*m));
    if(!b || size<128 || size>NEXIS_EDID_MAX_BYTES || size%128 || memcmp(b,header,8) || b[18]!=1 || b[19]<3 || b[19]>4)return false;
    unsigned blocks=(unsigned)b[126]+1;if(blocks>size/128)return false;
    for(unsigned i=0;i<blocks;i++)if(!checksum(b+i*128,128))return false;
    m->digital=(b[20]&128)!=0;m->product_id=(uint16_t)u16(b+10);
    unsigned manufacturer=(unsigned)b[8]<<8|b[9];
    for(unsigned i=0;i<3;i++){unsigned c=(manufacturer>>(10-i*5))&31;m->manufacturer[i]=c && c<=26?'A'+c-1:'?';}
    for(unsigned i=0;i<4;i++){
        const uint8_t *d=b+54+18*i;
        if(!u16(d) && d[2]==0 && d[3]==0xfc){
            unsigned n=0;while(n<13 && d[5+n]>=32 && d[5+n]<127){m->name[n]=(char)d[5+n];n++;}
            while(n && m->name[n-1]==' ')n--;
            m->name[n]=0;
        }else dtd(m,d,i==0 && ((b[24]&2) || b[19]>=4));
    }
    for(unsigned i=0;i<8;i++){
        const uint8_t *s=b+38+i*2;if((s[0]==1 && s[1]==1) || !s[0])continue;
        unsigned width=(s[0]+31)*8,hz=(s[1]&63)+60,ratio=s[1]>>6;
        unsigned height=ratio==0?width*10/16:ratio==1?width*3/4:ratio==2?width*4/5:width*9/16;
        dmt(m,width,height,hz);
    }
    static const uint16_t established[17][3]={
        {720,400,70},{720,400,88},{640,480,60},{640,480,67},{640,480,72},{640,480,75},{800,600,56},{800,600,60},
        {800,600,72},{800,600,75},{832,624,75},{1024,768,87},{1024,768,60},{1024,768,70},{1024,768,75},{1280,1024,75},{1152,870,75}};
    for(unsigned i=0;i<17;i++)if(b[35+i/8]&(128>>(i%8)))dmt(m,established[i][0],established[i][1],established[i][2]);
    for(unsigned i=1;i<blocks;i++){
        const uint8_t *e=b+128*i;
        if(e[0]==2){
            if(!e[1]){m->incomplete=true;continue;}
            unsigned end=e[2];if(end && (end<4 || end>127))return false;
            if(e[1]>=2 && (e[3]&64))m->stereo_48k16=true;
            if(end){
                if(e[1]>=3 && !cta_blocks(m,e,4,end))return false;
                unsigned native=e[3]&15,index=0;
                for(unsigned off=end;off+18<=127;off+=18)if(u16(e+off))dtd(m,e+off,index++<native);
            }
        }else if(e[0]==0x70){if(!displayid(m,e))return false;}
        else if(e[0]!=0xf0)m->incomplete=true;
    }
    m->valid=true;return true;
}
const edid_timing *edid_select_rgb8(const edid_monitor *m,uint32_t width,uint32_t height,const edid_link_limits *link){
    if(!m || !m->valid || !m->digital || !link || !link->max_pixel_khz || !width || !height)return NULL;
    const edid_timing *best=NULL;
    for(unsigned i=0;i<m->count;i++){
        const edid_timing *t=&m->modes[i];
        if(t->hactive!=width || t->vactive!=height || t->clock_khz>link->max_pixel_khz || t->flags&(EDID_INTERLACE|EDID_DOUBLE_CLOCK|EDID_Y420_ONLY))continue;
        if(link->hdmi){
            unsigned sink_clock=m->max_tmds_khz?m->max_tmds_khz:165000;
            if(!link->max_tmds_khz || t->clock_khz>link->max_tmds_khz || t->clock_khz>sink_clock)continue;
            if(t->clock_khz>340000 && !(m->scdc && link->scdc))continue;
        }
        if(!best || edid_refresh_millihz(t)>edid_refresh_millihz(best) ||
           (edid_refresh_millihz(t)==edid_refresh_millihz(best) && t->clock_khz<best->clock_khz))best=t;
    }
    return best;
}
