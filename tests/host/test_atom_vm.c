#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/gpu-driver/amd/atom_vm.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s (VM: %s)\n",__LINE__,#x,atom_vm_error_string(vm->error));exit(1);}}while(0)
static atom_vm *vm;
static uint8_t rom_bytes[32768],scratch[1032];
static uint32_t regs[3][1024],parameters[256];
static unsigned reads,writes,transactions,fail_at,cases,pc,base;
static unsigned clock_calls,fail_clock_at,time_fault;
static uint64_t now;
static bool read_reg(void *ctx,enum atom_vm_space space,uint32_t index,uint32_t *out){
    (void)ctx;transactions++;if(transactions==fail_at)return false;
    if(space>ATOM_VM_MC || index>=1024)return false;
    reads++;*out=regs[space][index];
    if(time_fault==1)now-=500;else if(time_fault==2)now+=3000000;
    return true;
}
static bool write_reg(void *ctx,enum atom_vm_space space,uint32_t index,uint32_t value){
    (void)ctx;transactions++;if(transactions==fail_at)return false;
    if(space>ATOM_VM_MC || index>=1024)return false;
    writes++;regs[space][index]=value;
    if(time_fault==3)now-=500;else if(time_fault==4)now+=3000000;
    return true;
}
static bool delay(void *ctx,uint32_t us){
    (void)ctx;now+=us;
    if(time_fault==5)now-=500;else if(time_fault==6)now+=3000000;
    return true;
}
static uint64_t clock_us(void *ctx){
    (void)ctx;if(++clock_calls==fail_clock_at)now-=500;
    uint64_t sample=now;if(now!=UINT64_MAX)now++;
    return sample;
}
static void w16(unsigned off,unsigned value){rom_bytes[off]=(uint8_t)value;rom_bytes[off+1]=(uint8_t)(value>>8);}
static void checksum(void){unsigned sum=0;rom_bytes[32767]=0;for(unsigned i=0;i<32767;i++)sum+=rom_bytes[i];rom_bytes[32767]=(uint8_t)(0-sum);}
static void fixture(void){
    memset(rom_bytes,0,sizeof(rom_bytes));memset(regs,0,sizeof(regs));memset(parameters,0,sizeof(parameters));memset(scratch,0,sizeof(scratch));
    scratch[1024]=0x91;scratch[1031]=0x73;reads=writes=transactions=fail_at=0;now=0;
    clock_calls=fail_clock_at=time_fault=0;
    rom_bytes[0]=0x55;rom_bytes[1]=0xaa;w16(0x18,0x60);memcpy(rom_bytes+0x60,"PCIR",4);
    w16(0x64,0x1002);w16(0x66,0x73ff);w16(0x6a,24);w16(0x70,64);rom_bytes[0x75]=128;
    w16(0x48,0x80);w16(0x80,40);rom_bytes[0x82]=2;rom_bytes[0x83]=2;memcpy(rom_bytes+0x84,"ATOM",4);
    w16(0x9e,0x100);w16(0xa0,0x300);w16(0x100,196);w16(0x300,52);
    /* A normal data table, with known constant 0x12345678. */
    w16(0x300+4+1*2,0x390);w16(0x390,8);rom_bytes[0x394]=0x78;rom_bytes[0x395]=0x56;rom_bytes[0x396]=0x34;rom_bytes[0x397]=0x12;
}
static void begin(unsigned command,unsigned offset,unsigned workspace,unsigned parameter_bytes){
    w16(0x100+4+command*2,offset);base=offset;pc=offset+6;
    rom_bytes[offset+2]=1;rom_bytes[offset+3]=1;rom_bytes[offset+4]=(uint8_t)workspace;rom_bytes[offset+5]=(uint8_t)parameter_bytes;
}
static void byte(unsigned value){rom_bytes[pc++]=(uint8_t)value;}
static void word(unsigned value){w16(pc,value);pc+=2;}
static void dword(uint32_t value){word(value&65535);word(value>>16);}
static void end(void){byte(91);w16(base,pc-base);}
static void emit(unsigned op,unsigned attr,unsigned index,uint32_t value){
    byte(op);byte(attr);
    /* Destinations: REG has a 16-bit address; other destinations use 8 bits. */
    if(op==1 || op==7 || op==13 || op==19 || op==25 || op==31 || op==37 || op==43 || op==49 || op==60 || op==74 || op==84 || op==92 || op==103 || op==109 || op==115)word(index);else byte(index);
    unsigned arg=attr&7,align=(attr>>3)&7;
    if(op>=84 && op<=89)return;
    if(op>=19 && op<=30){byte(value);return;}
    if(arg==0 || arg==3)word(value);
    else if(arg!=5 || align>=4)byte(value);
    else if(align)word(value);
    else dword(value);
}
static bool execute(unsigned words){
    checksum();atom_rom rom;atom_vm_io io={NULL,read_reg,write_reg,delay,clock_us};
    CHECK(atom_rom_open(rom_bytes,sizeof(rom_bytes),0x1002,0x73ff,&rom));
    CHECK(atom_vm_init(vm,&rom,&io,scratch,1024));bool result=atom_vm_execute(vm,12,parameters,words);
    CHECK(scratch[1024]==0x91 && scratch[1031]==0x73);cases++;return result;
}
static void basic(void){
    fixture();begin(12,0x400,8,16);
    emit(2,5,0,20);emit(44,5,0,7);emit(50,5,0,2);emit(8,5,0,31);emit(14,5,0,32);emit(104,5,0,1);
    emit(3,1,0,0);emit(32,5,0,3);emit(2,2,1,0x40);emit(38,5,0,4);emit(2,2,2,0x40);emit(2,2,3,0x41);
    emit(20,0,0,2);emit(26,0,0,1);emit(110,5,0,1);emit(116,5,0,2);emit(85,0,4,0);
    emit(1,1,4,0);emit(5,5,2,0x17);emit(6,5,3,0x19);end();
    CHECK(execute(16));CHECK(parameters[0]==56 && parameters[1]==168 && parameters[2]==14 && parameters[3]==0 && parameters[4]==0);
    CHECK(regs[0][4]==56 && regs[1][2]==0x17 && regs[2][3]==0x19);CHECK(writes==3);
}
static void lanes(void){
    fixture();begin(12,0x400,4,16);parameters[0]=0x11223344;
    byte(2);byte(5|4<<3|2<<6);byte(0);byte(0xfe); /* byte -> destination byte 16 */
    byte(2);byte(1|2<<3|1<<6);byte(1);byte(0); /* word8 -> destination word8 */
    byte(2);byte(5|1<<3);byte(2);word(0x1234); /* word immediate is only 2 bytes */
    byte(2);byte(5|7<<3|3<<6);byte(3);byte(0x80);
    emit(110,5|4<<3|1<<6,0,8); /* full saved destination shift then slice, not narrow-byte shift */
    end();CHECK(execute(16));CHECK(parameters[0]==0x11fe4444 && parameters[1]==0x00fe3300 && parameters[2]==0x1234 && parameters[3]==0x80000000);
}
static void control(void){
    fixture();begin(12,0x400,4,16);emit(2,5,0,2);emit(61,5,0,2);byte(68);unsigned target=pc;word(0);
    emit(2,5,1,99);w16(target,pc-base);emit(75,5,0,1);byte(68);target=pc;word(0);emit(2,5,1,88);w16(target,pc-base);
    /* SWITCH PS0: choose second case, then return through an early EOT. */
    byte(66);byte(1);byte(0);byte(0x63);dword(1);unsigned first=pc;word(0);byte(0x63);dword(2);unsigned second=pc;word(0);word(0x5a5a);
    w16(first,pc-base);emit(2,5,2,11);byte(91);w16(second,pc-base);emit(2,5,2,22);end();
    CHECK(execute(16));CHECK(parameters[1]==0 && parameters[2]==22);
    fixture();begin(12,0x400,4,16);emit(2,5,0,3);unsigned loop=pc-base;emit(50,5,0,1);emit(61,5,0,0);byte(73);word(loop);end();
    CHECK(execute(16));CHECK(parameters[0]==0);
}
static void nested(void){
    fixture();begin(13,0x800,2,4);emit(2,5,0,123);end();begin(12,0x400,2,16);byte(82);byte(13);byte(82);byte(14);end();
    CHECK(execute(16));CHECK(parameters[4]==123 && !parameters[0]);
    fixture();begin(12,0x400,2,0);byte(82);byte(12);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_LIMIT && !transactions);
    fixture();begin(12,0x400,2,16);byte(82);byte(13);end();begin(13,0x800,2,4);emit(2,5,0,7);end();CHECK(!execute(4));CHECK(!transactions);
}
static void memory(void){
    fixture();begin(12,0x400,4,16);
    byte(58);word(8);emit(1,5,2,7);byte(58);word(0);emit(1,5,0,3);
    byte(102);byte(1);emit(2,3,0,4);byte(59);byte(5);dword(1020);emit(4,5,0,0x80ff2211);emit(2,4,1,0);
    byte(102);byte(255);byte(122);word(3);byte(0);byte(255);byte(67);end();
    CHECK(execute(16));CHECK(regs[0][10]==7 && regs[0][0]==12 && parameters[0]==0x12345678 && parameters[1]==0x80ff2211);
    fixture();begin(12,0x400,4,16);byte(59);byte(5);dword(1024);emit(4,5,0,1);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_BOUNDS);
    fixture();begin(12,0x400,4,16);emit(3,5,0x43,32);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_BOUNDS);
    fixture();begin(12,0x400,4,16);emit(2,5,0,1);emit(20,0,0,32);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_BOUNDS);
    fixture();begin(12,0x400,4,16);byte(102);byte(255);emit(2,3,0,65535);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_BOUNDS);
}
static void wide_math(void){
    fixture();begin(12,0x400,4,16);parameters[0]=0xffffffff;
    emit(123,5,0,0xffffffff);emit(2,2,1,0x40);emit(2,2,2,0x41);emit(125,5,1,2);emit(2,2,3,0x40);emit(2,2,4,0x41);end();
    CHECK(execute(16));CHECK(parameters[1]==1 && parameters[2]==0xfffffffe && parameters[3]==0 && parameters[4]==0x7fffffff);
    fixture();begin(12,0x400,4,16);emit(38,5,0,0);emit(125,5,0,0);end();CHECK(execute(16));CHECK(!vm->divmul[0] && !vm->divmul[1]);
}
static void malformed(void){
    for(unsigned op=0;op<=255;op++)if(op==0 || op==56 || op==57 || op==83 || op==100 || op==101 || op>=127){
        fixture();begin(12,0x400,4,16);emit(1,5,4,1);byte(op);end();CHECK(!execute(16));CHECK(!transactions);
    }
    fixture();begin(12,0x400,4,16);emit(1,5,4,1);byte(67);word(7);end();CHECK(!execute(16));CHECK(!transactions);
    fixture();begin(12,0x400,4,16);emit(1,5,4,1);emit(2,1,0,255);end();CHECK(!execute(16));CHECK(!transactions);
    fixture();begin(12,0x400,4,16);emit(1,5,4,1);emit(3,2,9,0);end();CHECK(!execute(16));CHECK(!transactions);
    fixture();begin(12,0x400,4,16);byte(1);byte(5);word(2);byte(1);w16(base,pc-base);CHECK(!execute(16));CHECK(!transactions);
    fixture();begin(12,0x400,4,16);byte(67);word(6);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_LIMIT);
    fixture();begin(12,0x400,4,16);byte(80);byte(255);byte(67);word(6);end();CHECK(!execute(16));CHECK(vm->error==ATOM_VM_LIMIT);
    for(unsigned nth=1;nth<=4;nth++){
        fixture();begin(12,0x400,4,16);emit(1,5,4,1);emit(7,5,4,7);emit(1,0,6,4);end();fail_at=nth;
        CHECK(!execute(16));CHECK(vm->error==ATOM_VM_IO && transactions==nth);
    }
}
static void indirect_io(void){
    fixture();w16(0x300+4+23*2,0x350);unsigned p=0x354;
    rom_bytes[p++]=1;rom_bytes[p++]=1;rom_bytes[p++]=2;w16(p,3);p+=2;rom_bytes[p++]=9;p+=2;
    rom_bytes[p++]=1;rom_bytes[p++]=129;rom_bytes[p++]=2;w16(p,3);p+=2;
    rom_bytes[p++]=8;rom_bytes[p++]=32;rom_bytes[p++]=0;rom_bytes[p++]=0;
    rom_bytes[p++]=3;w16(p,4);p+=2;rom_bytes[p++]=9;p+=2;w16(0x350,p-0x350);
    regs[0][3]=0xa1234567;begin(12,0x400,4,16);byte(55);word(1);emit(2,0,0,9);emit(1,5,9,0x89abcdef);end();
    CHECK(execute(16));CHECK(parameters[0]==0xa1234567 && regs[0][4]==0x89abcdef);
    /* Reject an indirect descriptor with a 33-bit field at initialization. */
    rom_bytes[0x354+2+3+3+2+3+1]=33;checksum();atom_rom rom;atom_vm_io io={NULL,read_reg,write_reg,delay,clock_us};
    CHECK(atom_rom_open(rom_bytes,sizeof(rom_bytes),0x1002,0x73ff,&rom));CHECK(!atom_vm_init(vm,&rom,&io,scratch,1024));cases++;
    CHECK(!atom_vm_execute(vm,12,parameters,16));cases++;
}
static void mutations(void){
    uint32_t seed=0x73ff;
    for(unsigned n=0;n<3000;n++){
        fixture();begin(12,0x400,4,16);emit(2,5,0,1);emit(44,5,0,7);emit(1,1,4,0);end();
        seed=seed*1664525+1013904223;unsigned offset=0x400+seed%(pc-0x400);rom_bytes[offset]^=(uint8_t)(seed>>16);
        (void)execute(16);CHECK(vm->steps<=vm->step_limit+1);
    }
}
static void time_limits(void){
    fixture();begin(12,0x400,4,16);emit(1,5,4,1);emit(1,0,6,4);end();now=1000;
    CHECK(execute(16));unsigned samples=clock_calls;
    /* Every clock boundary, including after a posted write and the final
     * EOT, must reject backwards time without executing later operations. */
    for(unsigned n=2;n<=samples;n++){
        fixture();begin(12,0x400,4,16);emit(1,5,4,1);emit(1,0,6,4);end();now=1000;fail_clock_at=n;
        CHECK(!execute(16));CHECK(vm->error==ATOM_VM_IO && !vm->busy && clock_calls==n);
    }
    for(unsigned kind=1;kind<=6;kind++){
        fixture();begin(12,0x400,4,16);
        if(kind>=5){byte(81);byte(10);}else if(kind<=2)emit(2,0,0,4);else emit(1,5,4,1);
        emit(1,5,6,2);end();now=1000;time_fault=kind;
        CHECK(!execute(16));CHECK(vm->error==(kind&1?ATOM_VM_IO:ATOM_VM_LIMIT) && !vm->busy && !regs[0][6]);
        CHECK(writes==(kind==3 || kind==4?1:0));
    }
    fixture();begin(12,0x400,4,16);emit(1,5,4,1);end();now=UINT64_MAX;
    CHECK(!execute(16));CHECK(vm->error==ATOM_VM_LIMIT && !transactions && !vm->busy);
}
int main(void){
    vm=calloc(1,sizeof(*vm));if(!vm)return 1;
    basic();lanes();control();nested();memory();wide_math();malformed();indirect_io();mutations();time_limits();
    printf("{\"passed\":true,\"cases\":%u,\"board_bytecode_execution_implemented\":true,\"physical_modesetting_tested\":false}\n",cases);free(vm);return 0;
}
