#include "../../tools/gpu-driver/amd/atom_board.h"
#include "../../tools/gpu-driver/amd/atom_display_commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;static uint8_t bytes[2048];static atom_vm vm;
static void p16(unsigned at,unsigned value){bytes[at]=(uint8_t)value;bytes[at+1]=(uint8_t)(value>>8);}
static void p32(unsigned at,uint32_t value){for(unsigned n=0;n<4;n++)bytes[at+n]=(uint8_t)(value>>(8*n));}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned path_at(unsigned i){return 408+i*16;}
static unsigned records_at(unsigned count,unsigned i){return 408+count*16+i*24;}
static void init(unsigned revision,unsigned count,atom_rom *rom){
    memset(bytes,0,sizeof(bytes));*rom=(atom_rom){bytes,sizeof(bytes),80,100};
    p16(80,18);p16(100,64);bytes[102]=2;bytes[103]=1;
    p16(104+12*2,200);p16(104+22*2,400);p16(104+27*2,1600);
    p16(200,4+8*7);bytes[202]=2;bytes[203]=1;
    for(unsigned i=0;i<7;i++){
        p32(204+i*8,0x40000+i*4);bytes[208+i*8]=i;bytes[209+i*8]=i+1;bytes[210+i*8]=(uint8_t)(i<6?0x90+i:5);
    }
    p16(400,8+count*40);bytes[402]=1;bytes[403]=(uint8_t)revision;p16(404,0xec8);bytes[406]=(uint8_t)count;
    for(unsigned i=0;i<count;i++){
        unsigned p=path_at(i),r=records_at(count,i);
        /* Enumerate distinct connector objects; physical wiring can be muxed. */
        p16(p,0x3100+(i%6)*256+(i/6==0?ATOM_BOARD_HDMI:i/6==1?ATOM_BOARD_DP:0x14));
        p16(p+2,r-400);p16(p+4,0x211e);p16(p+12,8);
        bytes[r]=1;bytes[r+1]=4;bytes[r+2]=0x90;
        bytes[r+4]=2;bytes[r+5]=4;bytes[r+6]=5;bytes[r+7]=1;
        if(revision==4){p16(p+8,r+10-400);bytes[r+8]=255;bytes[r+9]=0;}
        else {bytes[r+8]=25;bytes[r+9]=2;} /* Unknown valid record is skipped. */
        bytes[r+10]=20;bytes[r+11]=6;p32(r+12,4|8);bytes[r+16]=255;
    }
    p16(1600,84);bytes[1602]=4;bytes[1603]=4;p32(1608,60000);p16(1612,2700);p16(1614,2700);
    p16(1636,2700);bytes[1640]=2;bytes[1641]=3;bytes[1642]=5;bytes[1644]=6;bytes[1645]=6;bytes[1646]=6;
}
static void check_empty(const atom_board *b){atom_board empty={0};CHECK(!memcmp(b,&empty,sizeof(*b)));}
static void normal(void){
    atom_rom rom;atom_board b;
    for(unsigned revision=4;revision<=5;revision++)for(unsigned count=1;count<=16;count++){
        init(revision,count,&rom);CHECK(atom_board_open(&rom,&b)==ATOM_BOARD_OK && b.count==count);
        CHECK(b.reference_khz==27000 && b.i2c_reference_khz==27000 && b.phy_reference_khz==27000 && b.boot_display_khz==600000);
        CHECK(b.pipes==5 && b.phys==6 && b.paths[0].connector==0x310c && b.paths[0].encoder==0x211e);
        CHECK(b.paths[0].internal_phy && b.paths[0].phy==0 && b.paths[0].ddc_hardware && b.paths[0].ddc_engine==1 && b.paths[0].ddc_line==0);
        CHECK(b.paths[0].ddc_gpio.index==0x40000 && b.paths[0].ddc_gpio.shift==0 && b.paths[0].ddc_gpio.mask_shift==1);
        CHECK(b.paths[0].has_hpd && b.paths[0].hpd_gpio.index==0x40018 && b.paths[0].hpd_gpio.shift==6 && b.paths[0].hpd_state==1);
        CHECK(b.paths[0].has_encoder_caps && b.paths[0].encoder_caps==12);cases++;
    }
    unsigned ids[]={0x211e,0x221e,0x2120,0x2220,0x2121,0x2221,0x2125};
    for(unsigned i=0;i<7;i++){unsigned phy=99;CHECK(atom_board_phy((uint16_t)ids[i],&phy) && phy==i);cases++;}
    unsigned bad[]={0,0x111e,0x201e,0x231e,0x2225,0x2155};
    for(unsigned i=0;i<6;i++){unsigned phy=99;CHECK(!atom_board_phy((uint16_t)bad[i],&phy) && phy==99);cases++;}
    init(4,1,&rom);bytes[204+6]=0x01; /* Software GPIO I2C is decoded, not silently upgraded. */
    bytes[records_at(1,0)+2]=0x01;CHECK(atom_board_open(&rom,&b)==ATOM_BOARD_OK && !b.paths[0].ddc_hardware);cases++;
}
static void malformed(void){
    atom_rom rom;atom_board b;
    for(unsigned kind=0;kind<29;kind++){
        init(4,1,&rom);unsigned p=path_at(0),r=records_at(1,0);
        switch(kind){
          case 0:bytes[403]=3;break;case 1:bytes[203]=2;break;case 2:bytes[1603]=6;break;
          case 3:p16(200,59);break;case 4:p16(400,23);break;case 5:bytes[406]=17;break;
          case 6:p16(p+2,8);break;case 7:p16(p+2,48);break;case 8:bytes[r+1]=1;break;
          case 9:bytes[r+1]=25;break;case 10:bytes[r+5]=3;break;case 11:bytes[r+7]=2;break;
          case 12:bytes[r+6]=99;break;case 13:bytes[r+6]=0x90;break;case 14:bytes[r+2]=0x99;break;
          case 15:bytes[208]=32;break;case 16:bytes[209]=32;break;case 17:p32(204,0);break;
          case 18:p32(204,0x40000000);break;case 19:bytes[210+8]=0x90;break;
          case 20:p16(1614,0);break;case 21:bytes[1642]=0;break;case 22:bytes[1645]=9;break;
          case 23:p16(p,0x300c);break;case 24:p16(p+4,0x2125);break;
          case 25:bytes[r+11]=5;break;case 26:p16(400,8+16+16);break;
          case 27:bytes[r+8]=1;bytes[r+9]=4;break;case 28:p32(1608,UINT32_MAX);break;
        }
        memset(&b,0xa5,sizeof(b));CHECK(atom_board_open(&rom,&b)!=ATOM_BOARD_OK);check_empty(&b);cases++;
    }
    init(4,2,&rom);p16(path_at(1),0x310c);CHECK(atom_board_open(&rom,&b)==ATOM_BOARD_AMBIGUOUS);check_empty(&b);cases++;
    init(4,1,&rom);p16(path_at(0)+6,0x21ff);CHECK(atom_board_open(&rom,&b)==ATOM_BOARD_OK && b.paths[0].external_encoder==0x21ff);cases++;
    for(unsigned size=0;size<1684;size++){init(4,1,&rom);rom.size=size;CHECK(atom_board_open(&rom,&b)!=ATOM_BOARD_OK);check_empty(&b);cases++;}
    uint32_t rng=0x3b251ef1;
    for(unsigned i=0;i<3000;i++){
        init(4+(i&1),1,&rom);rng=rng*1664525+1013904223;unsigned at=rng%sizeof(bytes);bytes[at]^=(uint8_t)(1u<<(rng>>29));
        struct {atom_board board;uint64_t guard;} out;memset(&out,0,sizeof(out));out.guard=0x1234aabbccddeeffULL;
        enum atom_board_error e=atom_board_open(&rom,&out.board);CHECK(out.guard==0x1234aabbccddeeffULL);
        if(e)check_empty(&out.board);else CHECK(out.board.count<=16 && out.board.count>0);cases++;
    }
}
static bool rd(void *c,enum atom_vm_space s,uint32_t i,uint32_t *v){(void)c;(void)s;(void)i;*v=0;return true;}
static bool wr(void *c,enum atom_vm_space s,uint32_t i,uint32_t v){(void)c;(void)s;(void)i;(void)v;return true;}
static bool delay(void *c,uint32_t us){(void)c;(void)us;return true;}
static uint64_t time_us(void *c){(void)c;return 100;}
static void commands(void){
    atom_rom rom;atom_board b;uint8_t output[65];init(4,1,&rom);CHECK(atom_board_open(&rom,&b)==ATOM_BOARD_OK);
    for(unsigned phy=0;phy<6;phy++)for(unsigned stream=0;stream<6;stream++)for(unsigned hpd=0;hpd<6;hpd++){
        atom_board_path p=b.paths[0];p.phy=(uint8_t)phy;
        unsigned ids[]={0x211e,0x221e,0x2120,0x2220,0x2121,0x2221};p.encoder=(uint16_t)ids[phy];
        memset(output,0xa5,sizeof(output));CHECK(atom_hdmi_pixel_parameters(output+1,&p,stream,558100));
        CHECK(output[0]==0xa5 && output[17]==0xa5 && u32(output+1)==5581000 && output[5]==20+phy && output[9]==stream && output[8]==1);
        CHECK(atom_hdmi_stream_parameters(output+1,stream,558100));CHECK(output[1]==stream && output[2]==15 && output[3]==3 && output[4]==4 && output[9]==2);
        CHECK(atom_hdmi_transmitter_parameters(output+1,&p,stream,hpd,558100,10));
        CHECK(output[1]==phy && output[2]==10 && output[9]==hpd+1 && output[10]==(1u<<stream) && output[11]==ATOM_BOARD_HDMI && u32(output+5)==55810);
        memset(output,0xa5,sizeof(output));CHECK(atom_hdmi_transmitter_v7_parameters(output+1,&p,stream,hpd,558100,10));
        CHECK(output[0]==0xa5 && output[1]==phy && output[9]==hpd+1 && output[10]==(1u<<stream) && u32(output+5)==55810);
        for(unsigned n=12;n<61;n++)CHECK(!output[n]);
        CHECK(output[61]==0xa5 && output[64]==0xa5);cases++;
    }
    for(unsigned action=0;action<256;action++){
        bool allowed=action==0 || action==1 || action==7 || action==8 || action==9 || action==10 || action==12 || action==13;
        memset(output,0xa5,sizeof(output));CHECK(atom_hdmi_transmitter_parameters(output+1,&b.paths[0],0,0,148500,action)==allowed);
        if(!allowed){for(unsigned n=0;n<33;n++)CHECK(output[n]==0xa5);}
        cases++;
    }
    b.paths[0].phy=1;CHECK(!atom_hdmi_pixel_parameters(output,&b.paths[0],0,148500));b.paths[0].phy=0;cases++;
    b.paths[0].encoder_caps=0;CHECK(!atom_hdmi_pixel_parameters(output,&b.paths[0],0,558100));
    CHECK(atom_hdmi_pixel_parameters(output,&b.paths[0],0,340000));b.paths[0].external_encoder=1;
    CHECK(!atom_hdmi_pixel_parameters(output,&b.paths[0],0,148500));cases++;
    /* Real bounded VM adapter, unaligned byte buffer, compatible table then
     * incompatible revision/parameter count: no implicit firmware fallback. */
    init(4,1,&rom);rom.commands=1400;p16(1400,4+77*2);p16(1404+12*2,1800);p16(1800,7);bytes[1802]=1;bytes[1803]=7;bytes[1805]=16;bytes[1806]=91;
    bytes[0]=0x55;bytes[1]=0xaa;bytes[2]=4;p16(0x18,280);memcpy(bytes+280,"PCIR",4);
    p16(284,0x1002);p16(286,0x73ff);p16(290,24);p16(296,4);bytes[301]=128;
    p16(0x48,1720);p16(1720,36);memcpy(bytes+1724,"ATOM",4);p16(1750,rom.commands);p16(1752,rom.data);
    unsigned sum=0;for(unsigned i=0;i<2047;i++)sum+=bytes[i];bytes[2047]=(uint8_t)(0-sum);
    uint32_t scratch[4]={0};atom_vm_io io={NULL,rd,wr,delay,time_us};CHECK(atom_vm_init(&vm,&rom,&io,(uint8_t *)scratch,sizeof(scratch)));
    memset(output,0xa5,sizeof(output));CHECK(atom_display_execute(&vm,ATOM_DISPLAY_PIXEL_CLOCK,output+1,16)==ATOM_VM_OK && output[0]==0xa5 && output[17]==0xa5);cases++;
    bytes[1803]=6;CHECK(atom_display_execute(&vm,ATOM_DISPLAY_PIXEL_CLOCK,output+1,16)==ATOM_VM_UNSUPPORTED);cases++;
    bytes[1803]=7;bytes[1805]=20;CHECK(atom_display_execute(&vm,ATOM_DISPLAY_PIXEL_CLOCK,output+1,16)==ATOM_VM_INPUT);cases++;
    CHECK(atom_display_execute(&vm,(enum atom_display_command)3,output+1,16)==ATOM_VM_TABLE);cases++;
    p16(1404+76*2,1900);p16(1900,7);bytes[1902]=1;bytes[1903]=7;bytes[1905]=60;bytes[1906]=91;
    CHECK(atom_display_execute(&vm,ATOM_DISPLAY_TRANSMITTER,output+1,60)==ATOM_VM_OK);cases++;
    CHECK(atom_display_execute(&vm,ATOM_DISPLAY_TRANSMITTER,output+1,32)==ATOM_VM_INPUT);cases++;
    bytes[1903]=6;bytes[1905]=32;CHECK(atom_display_execute(&vm,ATOM_DISPLAY_TRANSMITTER,output+1,32)==ATOM_VM_OK);cases++;
}
int main(void){normal();malformed();commands();printf("{\"passed\":true,\"cases\":%u,\"board_wiring_parser\":true,\"board_display_command_adapter\":true,\"physical_card_driver\":false,\"physical_hardware_verified\":false}\n",cases);return 0;}
