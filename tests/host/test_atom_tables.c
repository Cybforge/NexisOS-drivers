#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/gpu-driver/amd/atom_tables.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t bytes[2048];
static void w16(unsigned off,unsigned value){bytes[off]=(uint8_t)value;bytes[off+1]=(uint8_t)(value>>8);}
static void fix(void){bytes[1023]=0;unsigned sum=0;for(unsigned i=0;i<1023;i++)sum+=bytes[i];bytes[1023]=(uint8_t)(0-sum);}
static void fixture(void){
    memset(bytes,0,sizeof(bytes));bytes[0]=0x55;bytes[1]=0xaa;w16(0x18,0x60);memcpy(bytes+0x60,"PCIR",4);
    w16(0x64,0x1002);w16(0x66,0x73ff);w16(0x6a,24);w16(0x70,2);bytes[0x75]=128;
    w16(0x48,0x80);w16(0x80,40);bytes[0x82]=2;bytes[0x83]=2;memcpy(bytes+0x84,"ATOM",4);
    w16(0x9e,0xc0);w16(0xa0,0x180);w16(0xc0,166);bytes[0xc2]=2;bytes[0xc3]=1;
    w16(0xc0+4+12*2,0x200);w16(0x180,6);w16(0x184,0x240);
    w16(0x200,7);bytes[0x202]=1;bytes[0x203]=7;bytes[0x204]=16;bytes[0x205]=16;bytes[0x206]=0x5b;
    w16(0x240,8);bytes[0x242]=2;bytes[0x243]=1;fix();
}
int main(void){
    atom_rom rom;atom_table table;unsigned cases=0;uint8_t params[16];
    fixture();CHECK(atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));CHECK(rom.size==1024);
    CHECK(atom_rom_table(&rom,true,12,&table) && table.size==7 && table.revision==7 && table.workspace==16 && table.parameters==16);
    CHECK(atom_rom_table(&rom,false,0,&table) && table.size==8);cases++;
    CHECK(!atom_rom_table(&rom,true,81,&table) && !table.bytes);CHECK(!atom_rom_table(&rom,true,80,&table));cases++;
    CHECK(!atom_rom_open(bytes,1024,0x1002,0x73df,&rom));CHECK(!atom_rom_open(bytes,1024,0x8086,0x73ff,&rom));cases++;
    for(unsigned n=0;n<1024;n++){fixture();CHECK(!atom_rom_open(bytes,n,0x1002,0x73ff,&rom));cases++;}
    for(unsigned off=0;off<128;off++){
        fixture();w16(0x18,1024-off);fix();CHECK(!atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));cases++;
    }
    fixture();bytes[512]^=1;CHECK(!atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));cases++;
    fixture();w16(0xc0,165);fix();CHECK(!atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));cases++;
    fixture();w16(0x80,65535);fix();CHECK(!atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));cases++;
    fixture();w16(0x200,1024);fix();CHECK(atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));CHECK(!atom_rom_table(&rom,true,12,&table));cases++;
    fixture();w16(0x200,5);fix();CHECK(atom_rom_open(bytes,1024,0x1002,0x73ff,&rom));CHECK(!atom_rom_table(&rom,true,12,&table));cases++;
    fixture();memcpy(bytes+1024,bytes,1024);bytes[0x74]=3;bytes[0x75]=0;fix();
    CHECK(atom_rom_open(bytes,2048,0x1002,0x73ff,&rom) && rom.image==bytes+1024);cases++;
    CHECK(atom_pixel_clock_v7(params,558100,2,1,3,4,0));
    CHECK(params[0]==0xc8 && params[1]==0x28 && params[2]==0x55 && params[3]==0); /* 5,581,000 x 100 Hz */
    CHECK(params[4]==1 && params[5]==3 && params[6]==4 && params[8]==2 && !params[9] && !params[15]);cases++;
    CHECK(!atom_pixel_clock_v7(params,0,0,0,0,0,0));CHECK(!atom_pixel_clock_v7(params,558100,6,0,0,0,0));cases++;
    printf("{\"passed\":true,\"cases\":%u,\"firmware_tables_executed\":false}\n",cases);return 0;
}
