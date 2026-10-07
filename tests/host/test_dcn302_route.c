#include "../../tools/gpu-driver/amd/dcn302_route.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
#define HS(v,f,n) (((v)&~DCN302_HDMI_##f##_MASK)|(((uint32_t)(n)<<DCN302_HDMI_##f##_SHIFT)&DCN302_HDMI_##f##_MASK))
static unsigned cases;
typedef struct {uint32_t otg[5][DCN302_REGISTER_COUNT],dig[5][DCN302_HDMI_REGISTER_COUNT],hpd[5];unsigned ops,fail,change_at;uint32_t change_offset,change_value;bool invalid;} model;
static uint32_t *lookup(model *m,uint32_t offset){
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_REGISTER_COUNT;r++)if(offset==dcn302_register_bytes[i][r])return &m->otg[i][r];
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(offset==dcn302_hdmi_register_bytes[i][r])return &m->dig[i][r];
        if(offset==dcn302_hpd_status_bytes[i])return &m->hpd[i];
    }
    m->invalid=true;return NULL;
}
static bool rd(void *c,uint32_t offset,uint32_t *v){
    model *m=c;if(++m->ops==m->fail)return false;
    if(m->change_at && m->ops==m->change_at){uint32_t *p=lookup(m,m->change_offset);CHECK(p);*p=m->change_value;}
    uint32_t *p=lookup(m,offset);if(!p)return false;*v=*p;return true;
}
static unsigned shift(uint32_t mask){unsigned n=0;CHECK(mask);while(!(mask&1)){n++;mask>>=1;}CHECK(mask==1);return n;}
static void init(model *m,dcn302_io *io,atom_board *b,unsigned link,unsigned stream,unsigned otg,unsigned ddc,unsigned hpd){
    memset(m,0,sizeof(*m));memset(b,0,sizeof(*b));*io=(dcn302_io){m,rd,NULL,NULL};
    b->count=1;b->pipes=5;b->phys=5;b->plls=5;b->i2c_reference_khz=27000;
    atom_board_path *p=&b->paths[0];p->connector=0x310c;p->encoder=0x211e;p->kind=ATOM_BOARD_HDMI;p->internal_phy=true;p->phy=(uint8_t)link;
    p->has_ddc=p->ddc_hardware=p->has_hpd=p->has_encoder_caps=true;p->encoder_caps=ATOM_BOARD_HDMI6G;
    p->ddc_line=(uint8_t)ddc;p->ddc_engine=1;p->ddc_gpio.index=dcn302_ddc_gpio_bytes[ddc]/4;p->ddc_gpio.shift=(uint8_t)shift(dcn302_ddc_clk_mask[ddc]);
    p->hpd_state=1;p->hpd_gpio.index=DCN302_GPIO_HPD_BYTES/4;p->hpd_gpio.shift=(uint8_t)shift(dcn302_hpd_mask[hpd]);
    m->hpd[hpd]=DCN302_HPD_SENSE_MASK|DCN302_HPD_DELAYED_MASK;
    m->dig[link][DCN302_HDMI_R_BE]=HS(HS(HS(0,LINK_MODE,3),FE_SOURCE,1u<<stream),HPD,hpd);
    m->dig[link][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
    m->dig[stream][DCN302_HDMI_R_FE]=HS(0,PIPE,otg);
    uint32_t *o=m->otg[otg];o[DCN302_R_CONTROL]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    o[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;
    o[DCN302_R_SOURCE]=((otg+1)%5)<<DCN302_SEG0_SHIFT;
    o[DCN302_R_H_TOTAL]=2079;o[DCN302_R_H_BLANK]=2032|(112u<<16);o[DCN302_R_H_SYNC]=64u<<16;
    o[DCN302_R_V_TOTAL]=1117;o[DCN302_R_V_BLANK]=1115|(35u<<16);o[DCN302_R_V_SYNC]=5u<<16;
}
static void normal(void){
    model m;dcn302_io io;atom_board b;dcn302_route r;
    /* All permutations, not just DIG0/OTG0: firmware routes these independently. */
    for(unsigned link=0;link<5;link++)for(unsigned stream=0;stream<5;stream++)for(unsigned otg=0;otg<5;otg++)for(unsigned bus=0;bus<5;bus++)for(unsigned hpd=0;hpd<5;hpd++){
        init(&m,&io,&b,link,stream,otg,bus,hpd);CHECK(dcn302_route_find(&io,&b,1920,1080,&r)==DCN302_ROUTE_OK);
        CHECK(r.path==0 && r.link==link && r.stream==stream && r.otg==otg && r.opp==(otg+1)%5 && r.ddc==bus && r.hpd==hpd);
        CHECK(!r.shape.pixel_khz && r.shape.htotal==2080 && r.shape.hsync_start==1968 && r.shape.hsync_end==2032 && r.shape.vtotal==1118);
        CHECK(r.max_tmds_khz==600000 && !m.invalid);cases++;
    }
    init(&m,&io,&b,4,0,3,2,1);b.paths[0].has_encoder_caps=false;
    CHECK(dcn302_route_find(&io,&b,1920,1080,&r)==DCN302_ROUTE_OK && r.max_tmds_khz==340000);cases++;
    init(&m,&io,&b,4,0,3,2,1);b.paths[0].hpd_state=0;m.hpd[1]=0;
    CHECK(dcn302_route_find(&io,&b,1920,1080,&r)==DCN302_ROUTE_OK);cases++;
}
static void rejected(void){
    model m;dcn302_io io;atom_board b;dcn302_route r,zero={0};
    for(unsigned kind=0;kind<24;kind++){
        init(&m,&io,&b,4,0,3,2,1);
        switch(kind){
            case 0:b.count=0;break;case 1:b.count=17;break;case 2:m.dig[4][DCN302_HDMI_R_BE_ENABLE]=0;break;
            case 3:m.dig[2][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK;break;
            case 4:m.dig[4][DCN302_HDMI_R_BE]=HS(m.dig[4][DCN302_HDMI_R_BE],LINK_MODE,0);break;
            case 5:m.dig[4][DCN302_HDMI_R_BE]=HS(m.dig[4][DCN302_HDMI_R_BE],FE_SOURCE,3);break;
            case 6:m.dig[4][DCN302_HDMI_R_BE]=HS(m.dig[4][DCN302_HDMI_R_BE],FE_SOURCE,32);break;
            case 7:m.dig[0][DCN302_HDMI_R_FE]=HS(0,PIPE,6);break;
            case 8:m.dig[0][DCN302_HDMI_R_FE]|=DCN302_HDMI_RGB_ENCODING_MASK;break;
            case 9:m.dig[0][DCN302_HDMI_R_FE]|=DCN302_HDMI_COLOR_FORMAT_MASK;break;
            case 10:b.paths[0].external_encoder=1;break;case 11:b.paths[0].phy=2;break;
            case 12:b.paths[0].hpd_gpio.index++;break;case 13:b.paths[0].hpd_gpio.shift=31;break;
            case 14:b.paths[0].ddc_hardware=false;break;case 15:b.paths[0].ddc_gpio.index++;break;
            case 16:b.paths[0].ddc_line=1;break;case 17:b.paths[0].ddc_slave=80;break;
            case 18:b.paths[0].ddc_gpio.shift=32;break;case 19:m.hpd[1]=DCN302_HPD_SENSE_MASK;break;
            case 20:m.hpd[1]=0;break;case 21:m.otg[1][DCN302_R_CONTROL]=DCN302_MASTER_ACTIVE_MASK|DCN302_MASTER_ENABLE_MASK;break;
            case 22:b.paths[1]=b.paths[0];b.count=2;break;case 23:m.otg[3][DCN302_R_FORMAT]=DCN302_DSC_MASK;break;
        }
        memset(&r,0xa5,sizeof(r));CHECK(dcn302_route_find(&io,&b,1920,1080,&r)!=DCN302_ROUTE_OK && !memcmp(&r,&zero,sizeof(r)) && !m.invalid);cases++;
    }
    init(&m,&io,&b,4,0,3,2,1);CHECK(dcn302_route_find(&io,&b,1280,1080,&r)==DCN302_ROUTE_UNSUPPORTED);cases++;
    init(&m,&io,&b,4,0,3,2,1);CHECK(dcn302_route_find(&io,&b,1920,1080,&r)==DCN302_ROUTE_OK);unsigned ops=m.ops;
    for(unsigned fail=1;fail<=ops;fail++){
        init(&m,&io,&b,4,0,3,2,1);m.fail=fail;
        CHECK(dcn302_route_find(&io,&b,1920,1080,&r)!=DCN302_ROUTE_OK && !memcmp(&r,&zero,sizeof(r)) && !m.invalid);cases++;
    }
    for(unsigned kind=0;kind<4;kind++){
        init(&m,&io,&b,4,0,3,2,1);m.change_at=42;
        switch(kind){
            case 0:m.change_offset=dcn302_hdmi_register_bytes[4][DCN302_HDMI_R_BE];m.change_value=0;break;
            case 1:m.change_offset=dcn302_register_bytes[3][DCN302_R_H_TOTAL];m.change_value=2199;break;
            case 2:m.change_offset=dcn302_register_bytes[3][DCN302_R_CONTROL];m.change_value=0;break;
            case 3:m.change_offset=dcn302_hpd_status_bytes[1];m.change_value=0;break;
        }
        CHECK(dcn302_route_find(&io,&b,1920,1080,&r)!=DCN302_ROUTE_OK && !memcmp(&r,&zero,sizeof(r)));cases++;
    }
}
int main(void){normal();rejected();printf("{\"passed\":true,\"cases\":%u,\"native_board_pipeline_binding\":true,\"hardware_writes\":false,\"physical_card_driver\":false,\"physical_hardware_verified\":false}\n",cases);return 0;}
