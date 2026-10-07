/*
 * ATOM bytecode semantics adapted from Linux v6.12 amdgpu/atom.c.
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Author of the original interpreter: Stanislaw Skowronek
 * NexisOS bounded decoder/interpreter implementation, 2026.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "atom_vm.h"
#include <string.h>
enum { REG,PS,WS,ID,FB,IMM,PLL,MC };
enum { MOVE,AND,OR,LEFT,RIGHT,MUL,DIV,ADD,SUB,COMPARE,TEST,CLEAR,MASK,XOR,SHL,SHR,MUL32,DIV32 };
static const uint32_t masks[8]={0xffffffff,0xffff,0xffff00,0xffff0000,0xff,0xff00,0xff0000,0xff000000};
static const uint8_t shifts[8]={0,0,8,16,0,8,16,24};
static const uint8_t destinations[8][4]={{0,0,0,0},{1,2,3,0},{1,2,3,0},{1,2,3,0},{4,5,6,7},{4,5,6,7},{4,5,6,7},{4,5,6,7}};
static const uint8_t defaults[8]={0,0,1,2,0,1,2,3},args[6]={REG,PS,WS,FB,PLL,MC};
typedef struct { unsigned arg,align,index; } operand;
typedef struct {
    unsigned opcode,operation,attr,begin,end,target,extra;
    bool arithmetic;
    operand dst,src;
} instruction;
static unsigned le16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t le32(const uint8_t *p){return le16(p)|(uint32_t)le16(p+2)<<16;}
static bool fail(atom_vm *v,enum atom_vm_error e){if(v->error==ATOM_VM_OK)v->error=e;return false;}
static bool span(size_t n,size_t at,size_t bytes){return at<=n && bytes<=n-at;}
static bool tick(atom_vm *v){
    if(++v->steps>v->step_limit)return fail(v,ATOM_VM_LIMIT);
    if(v->io.time_us(v->io.context)>=v->deadline_us)return fail(v,ATOM_VM_LIMIT);
    return true;
}
static bool take(atom_vm *v,atom_vm_frame *f,unsigned *pc,unsigned n,uint32_t *out){
    if(!span(f->table.size,*pc,n))return fail(v,ATOM_VM_BYTECODE);
    const uint8_t *p=f->table.bytes+*pc;
    *out=n==4?le32(p):n==2?le16(p):*p;*pc+=n;return true;
}
static unsigned immediate_bytes(unsigned align){return align==0?4:align<4?2:1;}
static bool operand_decode(atom_vm *v,atom_vm_frame *f,unsigned *pc,unsigned arg,unsigned align,operand *out){
    uint32_t n=0;out->arg=arg;out->align=align;
    if(!take(v,f,pc,arg==REG || arg==ID?2:arg==IMM?immediate_bytes(align):1,&n))return false;
    out->index=n;
    if(arg==PS && n>=f->words)return fail(v,ATOM_VM_BOUNDS);
    if(arg==WS && !(n>=0x40 && n<=0x48) && n>=f->table.workspace)return fail(v,ATOM_VM_BOUNDS);
    return true;
}
static bool decode(atom_vm *v,atom_vm_frame *f,unsigned pc,instruction *i){
    memset(i,0,sizeof(*i));i->begin=pc;uint32_t op,a;
    if(!take(v,f,&pc,1,&op))return false;
    i->opcode=op;
    unsigned arg=0,operation=0;
    if(op>=1 && op<=54){operation=(op-1)/6;arg=args[(op-1)%6];i->arithmetic=true;}
    else if(op>=60 && op<=65){operation=COMPARE;arg=args[op-60];i->arithmetic=true;}
    else if(op>=74 && op<=79){operation=TEST;arg=args[op-74];i->arithmetic=true;}
    else if(op>=84 && op<=89){operation=CLEAR;arg=args[op-84];i->arithmetic=true;}
    else if(op>=92 && op<=97){operation=MASK;arg=args[op-92];i->arithmetic=true;}
    else if(op>=103 && op<=120){operation=XOR+(op-103)/6;arg=args[(op-103)%6];i->arithmetic=true;}
    else if(op>=123 && op<=126){operation=op<=124?MUL32:DIV32;arg=(op&1)?PS:WS;i->arithmetic=true;}
    i->operation=operation;
    if(i->arithmetic){
        if(!take(v,f,&pc,1,&a))return false;
        if(operation==LEFT || operation==RIGHT || operation==CLEAR)a=(a&0x38)|(unsigned)defaults[(a>>3)&7]<<6;
        i->attr=a;unsigned align=(a>>3)&7,dalign=destinations[align][(a>>6)&3];
        if(!operand_decode(v,f,&pc,arg,dalign,&i->dst))return false;
        if(operation==MASK && !take(v,f,&pc,immediate_bytes(align),&a))return false;
        if(operation==MASK)i->extra=a;
        if(operation==CLEAR){i->src=(operand){IMM,0,0};}
        else if(operation==LEFT || operation==RIGHT){if(!operand_decode(v,f,&pc,IMM,4,&i->src))return false;}
        else if(!operand_decode(v,f,&pc,i->attr&7,align,&i->src))return false;
    }else switch(op){
        case 55: case 58:
            if(!take(v,f,&pc,2,&a))return false;
            i->extra=a;
            if(op==55 && a>127)return fail(v,ATOM_VM_UNSUPPORTED);
            break;
        case 59:
            if(!take(v,f,&pc,1,&a) || !operand_decode(v,f,&pc,a&7,(a>>3)&7,&i->src))return false;
            break;
        case 66:{
            if(!take(v,f,&pc,1,&a) || !operand_decode(v,f,&pc,a&7,(a>>3)&7,&i->src))return false;
            i->attr=a;i->target=pc;
            for(;;){
                if(!span(f->table.size,pc,2))return fail(v,ATOM_VM_BYTECODE);
                if(le16(f->table.bytes+pc)==0x5a5a){pc+=2;break;}
                operand value;if(!take(v,f,&pc,1,&a) || a!=0x63)return fail(v,ATOM_VM_BYTECODE);
                if(!operand_decode(v,f,&pc,IMM,i->src.align,&value) || !take(v,f,&pc,2,&a))return false;
            }break;
        }
        case 67: case 68: case 69: case 70: case 71: case 72: case 73:
            if(!take(v,f,&pc,2,&a))return false;
            i->target=a;break;
        case 80: case 81: case 82: case 98: case 102: case 121:
            if(!take(v,f,&pc,1,&a))return false;
            i->extra=a;break;
        case 90: case 91: case 99:break;
        case 122:
            if(!take(v,f,&pc,2,&a) || !span(f->table.size,pc,a))return fail(v,ATOM_VM_BYTECODE);
            pc+=a;break;
        /* PCI/SYSIO and undocumented repeat/save/restore operations do not
         * silently succeed. The driver must provide a supported command path. */
        default:return fail(v,ATOM_VM_UNSUPPORTED);
    }
    i->end=pc;return true;
}
static bool boundary(const atom_vm_frame *f,unsigned pc){return pc>=6 && pc<f->table.size && (f->boundaries[pc/8]&(1u<<(pc&7)));}
static bool preflight(atom_vm *v,unsigned command,uint32_t *ps,unsigned words,unsigned depth){
    if(depth>=ATOM_VM_DEPTH)return fail(v,ATOM_VM_LIMIT);
    atom_vm_frame *f=&v->frames[depth];memset(f,0,sizeof(*f));
    if(!atom_rom_table(&v->rom,true,command,&f->table))return fail(v,ATOM_VM_TABLE);
    f->parameters=ps;f->words=words;f->shift=f->table.parameters/4;
    if(f->table.parameters&3 || f->shift>words)return fail(v,ATOM_VM_BOUNDS);
    bool has_end=false;
    for(unsigned pc=6;pc<f->table.size;){
        /* Some boards pad command tables. Padding is never a branch target. */
        if(has_end && (f->table.bytes[pc]==0 || f->table.bytes[pc]==255)){
            unsigned p=pc;while(p<f->table.size && (f->table.bytes[p]==0 || f->table.bytes[p]==255))p++;
            if(p==f->table.size)break;
        }
        instruction i;if(!tick(v) || !decode(v,f,pc,&i))return false;
        f->boundaries[pc/8]|=1u<<(pc&7);if(i.opcode==91)has_end=true;pc=i.end;
    }
    if(!has_end)return fail(v,ATOM_VM_BYTECODE);
    for(unsigned pc=6;pc<f->table.size && boundary(f,pc);){
        instruction i;if(!tick(v) || !decode(v,f,pc,&i))return false;
        if(i.opcode>=67 && i.opcode<=73 && !boundary(f,i.target))return fail(v,ATOM_VM_BYTECODE);
        if(i.opcode==66){
            unsigned p=i.target;uint32_t n;
            while(le16(f->table.bytes+p)!=0x5a5a){
                operand value;p++;if(!operand_decode(v,f,&p,IMM,i.src.align,&value) || !take(v,f,&p,2,&n))return false;
                if(!boundary(f,n))return fail(v,ATOM_VM_BYTECODE);
            }
        }
        if(i.opcode==82){
            atom_table nested;
            /* A genuinely absent optional command is a no-op, matching ATOM.
             * An out-of-range index or malformed nonzero entry is rejected. */
            unsigned m=v->rom.commands,n=le16(v->rom.image+m);
            if(i.extra>=(n-4)/2)return fail(v,ATOM_VM_TABLE);
            if(le16(v->rom.image+m+4+i.extra*2)){
                if(!atom_rom_table(&v->rom,true,i.extra,&nested) || !preflight(v,i.extra,ps+f->shift,words-f->shift,depth+1))return false;
            }
        }
        pc=i.end;
    }
    return true;
}
static bool hardware_read(atom_vm *v,enum atom_vm_space space,unsigned reg,uint32_t *out){
    if(!v->io.read(v->io.context,space,reg,out))return fail(v,ATOM_VM_IO);
    return true;
}
static bool hardware_write(atom_vm *v,enum atom_vm_space space,unsigned reg,uint32_t value){
    if(!v->io.write(v->io.context,space,reg,value))return fail(v,ATOM_VM_IO);
    return true;
}
static bool indirect(atom_vm *v,unsigned method,unsigned index,uint32_t data,uint32_t *result){
    if(method>255 || !v->indirect_begin[method])return fail(v,ATOM_VM_UNSUPPORTED);
    const uint8_t *b=v->rom.image;unsigned pc=v->indirect_begin[method],end=v->indirect_end[method];uint32_t temp=0xcdcdcdcd;
    while(pc<end){
        unsigned op=b[pc],n=op==0?1:op<=5?3:4;if(!tick(v))return false;
        if(op==2){if(!hardware_read(v,ATOM_VM_MMIO,le16(b+pc+1),&temp))return false;}
        else if(op==3){if(!hardware_write(v,ATOM_VM_MMIO,le16(b+pc+1),temp))return false;}
        else if(op>=4){
            unsigned bits=b[pc+1],source=b[pc+2],dest=op<=5?source:b[pc+3];
            uint32_t mask=bits==32?0xffffffff:(1u<<bits)-1;
            if(op==4)temp&=~(mask<<dest);
            else if(op==5)temp|=mask<<dest;
            else{uint32_t value=op==6?index:op==7?v->io_attr:data;temp=(temp&~(mask<<dest))|((value>>source)&mask)<<dest;}
        }
        pc+=n;
    }
    *result=temp;return true;
}
static bool get(atom_vm *v,atom_vm_frame *f,operand o,uint32_t *value,uint32_t *raw){
    uint32_t n=o.index,x=0;
    switch(o.arg){
        case IMM:*value=n;if(raw)*raw=n;return true;
        case REG:
            if(n>0xffffffff-v->reg_block)return fail(v,ATOM_VM_BOUNDS);
            n+=v->reg_block;
            if(!v->io_mode){if(!hardware_read(v,ATOM_VM_MMIO,n,&x))return false;}
            else if(!indirect(v,v->io_mode&127,n,0,&x))return false;
            break;
        case PLL:case MC:if(!hardware_read(v,o.arg==PLL?ATOM_VM_PLL:ATOM_VM_MC,n,&x))return false;break;
        case PS:x=f->parameters[n];break;
        case WS:
            switch(n){
                case 0x40:x=v->divmul[0];break;case 0x41:x=v->divmul[1];break;
                case 0x42:x=v->data_block;break;case 0x43:x=v->bit_shift;break;
                case 0x44:x=1u<<v->bit_shift;break;case 0x45:x=~(1u<<v->bit_shift);break;
                case 0x46:x=v->fb_base;break;case 0x47:x=v->io_attr;break;case 0x48:x=v->reg_block;break;
                default:x=f->workspace[n];break;
            }break;
        case ID:
            if(n>0xffffffff-v->data_block || !span(v->rom.size,n+v->data_block,4))return fail(v,ATOM_VM_BOUNDS);
            x=le32(v->rom.image+n+v->data_block);break;
        case FB:
            if((v->fb_base&3) || !span(v->scratch_bytes,v->fb_base,(size_t)n*4+4))return fail(v,ATOM_VM_BOUNDS);
            x=le32(v->scratch+v->fb_base+n*4);break;
        default:return fail(v,ATOM_VM_UNSUPPORTED);
    }
    if(raw)*raw=x;
    *value=(x&masks[o.align])>>shifts[o.align];return true;
}
static bool put(atom_vm *v,atom_vm_frame *f,operand o,uint32_t x,uint32_t raw){
    uint32_t n=o.index,value=(raw&~masks[o.align])|((x<<shifts[o.align])&masks[o.align]);
    switch(o.arg){
        case REG:
            if(n>0xffffffff-v->reg_block)return fail(v,ATOM_VM_BOUNDS);
            n+=v->reg_block;
            if(!v->io_mode)return hardware_write(v,ATOM_VM_MMIO,n,n?value:value<<2);
            return indirect(v,v->io_mode,n,value,&x);
        case PLL:case MC:return hardware_write(v,o.arg==PLL?ATOM_VM_PLL:ATOM_VM_MC,n,value);
        case PS:f->parameters[n]=value;return true;
        case WS:
            switch(n){
                case 0x40:v->divmul[0]=value;break;case 0x41:v->divmul[1]=value;break;
                case 0x42:if(value>v->rom.size)return fail(v,ATOM_VM_BOUNDS);v->data_block=value;break;
                case 0x43:if(value>31)return fail(v,ATOM_VM_BOUNDS);v->bit_shift=value;break;
                case 0x44:case 0x45:break;
                case 0x46:if((value&3) || value>v->scratch_bytes)return fail(v,ATOM_VM_BOUNDS);v->fb_base=value;break;
                case 0x47:if(value>65535)return fail(v,ATOM_VM_BOUNDS);v->io_attr=value;break;
                case 0x48:if(value>65535)return fail(v,ATOM_VM_BOUNDS);v->reg_block=value;break;
                default:f->workspace[n]=value;break;
            }return true;
        case FB:
            if((v->fb_base&3) || !span(v->scratch_bytes,v->fb_base,(size_t)n*4+4))return fail(v,ATOM_VM_BOUNDS);
            for(unsigned i=0;i<4;i++)v->scratch[v->fb_base+n*4+i]=(uint8_t)(value>>(i*8));
            return true;
        default:return fail(v,ATOM_VM_UNSUPPORTED);
    }
}
static bool run(atom_vm *v,unsigned command,uint32_t *ps,unsigned words,unsigned depth){
    if(depth>=ATOM_VM_DEPTH)return fail(v,ATOM_VM_LIMIT);
    atom_vm_frame *f=&v->frames[depth];
    /* Rebuild this frame's boundaries: sibling calls reuse the next frame. */
    if(!preflight(v,command,ps,words,depth))return false;
    f->pc=6;
    for(;;){
        if(!tick(v) || !boundary(f,f->pc))return fail(v,ATOM_VM_BYTECODE);
        instruction i;if(!decode(v,f,f->pc,&i))return false;f->pc=i.end;
        if(i.arithmetic){
            uint32_t dst=0,raw=0,src;
            /* Full-width MOVE must not read write-only device registers. */
            if((i.operation!=MOVE || i.dst.align) && !get(v,f,i.dst,&dst,&raw))return false;
            if(!get(v,f,i.src,&src,NULL))return false;
            bool write=true;uint64_t wide;
            switch(i.operation){
                case MOVE:dst=src;break;case AND:dst&=src;break;case OR:dst|=src;break;
                case ADD:dst+=src;break;case SUB:dst-=src;break;case XOR:dst^=src;break;
                case LEFT:case RIGHT:case SHL:case SHR:
                    if(src>31)return fail(v,ATOM_VM_BOUNDS);
                    if(i.operation==LEFT)dst<<=src;else if(i.operation==RIGHT)dst>>=src;
                    else{dst=i.operation==SHL?raw<<src:raw>>src;dst=(dst&masks[i.dst.align])>>shifts[i.dst.align];}break;
                case CLEAR:dst=0;break;case MASK:dst=(dst&i.extra)|src;break;
                case COMPARE:v->equal=dst==src;v->above=dst>src;write=false;break;
                case TEST:v->equal=(dst&src)==0;write=false;break;
                case MUL:v->divmul[0]=dst*src;write=false;break;
                case MUL32:wide=(uint64_t)dst*src;v->divmul[0]=(uint32_t)wide;v->divmul[1]=(uint32_t)(wide>>32);write=false;break;
                case DIV:v->divmul[0]=src?dst/src:0;v->divmul[1]=src?dst%src:0;write=false;break;
                case DIV32:
                    wide=src?(((uint64_t)v->divmul[1]<<32)|dst)/src:0;
                    v->divmul[0]=(uint32_t)wide;v->divmul[1]=(uint32_t)(wide>>32);write=false;break;
                default:return fail(v,ATOM_VM_UNSUPPORTED);
            }
            if(write && !put(v,f,i.dst,dst,raw))return false;
        }else switch(i.opcode){
            case 55:v->io_mode=i.extra?i.extra|128:0;break;
            case 58:v->reg_block=i.extra;break;
            case 59:{
                uint32_t value;
                if(!get(v,f,i.src,&value,NULL))return false;
                if((value&3) || value>v->scratch_bytes)return fail(v,ATOM_VM_BOUNDS);
                v->fb_base=value;break;
            }
            case 66:{
                uint32_t value;if(!get(v,f,i.src,&value,NULL))return false;unsigned p=i.target;
                while(le16(f->table.bytes+p)!=0x5a5a){
                    operand choice;uint32_t target;p++;
                    if(!operand_decode(v,f,&p,IMM,i.src.align,&choice) || !take(v,f,&p,2,&target))return false;
                    if(choice.index==value){f->pc=target;break;}
                }break;
            }
            case 67:case 68:case 69:case 70:case 71:case 72:case 73:{
                bool take_jump=i.opcode==67 || (i.opcode==68 && v->equal) || (i.opcode==69 && !v->equal && !v->above) ||
                    (i.opcode==70 && v->above) || (i.opcode==71 && !v->above) || (i.opcode==72 && (v->above || v->equal)) || (i.opcode==73 && !v->equal);
                if(take_jump)f->pc=i.target;
                break;
            }
            case 80:case 81:{
                uint32_t us=i.extra*(i.opcode==80?1000:1);
                if(us>v->delay_remaining_us)return fail(v,ATOM_VM_LIMIT);
                v->delay_remaining_us-=us;if(!v->io.delay_us(v->io.context,us))return fail(v,ATOM_VM_IO);break;
            }
            case 82:{
                unsigned off=v->rom.commands+4+i.extra*2;
                if(le16(v->rom.image+off) && !run(v,i.extra,ps+f->shift,words-f->shift,depth+1))return false;
                break;
            }
            case 91:return true;
            case 102:
                if(!i.extra)v->data_block=0;
                else if(i.extra==255)v->data_block=(uint32_t)(f->table.bytes-v->rom.image);
                else{atom_table table;if(!atom_rom_table(&v->rom,false,i.extra,&table))return fail(v,ATOM_VM_TABLE);v->data_block=(uint32_t)(table.bytes-v->rom.image);}break;
            default:break; /* validated NOP/EOT diagnostics/inline data only */
        }
    }
}
bool atom_vm_init(atom_vm *v,const atom_rom *rom,const atom_vm_io *io,uint8_t *scratch,size_t bytes){
    if(!v)return false;
    memset(v,0,sizeof(*v));v->error=ATOM_VM_INPUT;
    if(!rom || !rom->image || rom->size<512 || rom->size>1024*1024 || !io || !io->read || !io->write || !io->delay_us || !io->time_us ||
       (bytes && !scratch) || bytes>65536 || (bytes&3))return false;
    unsigned pci=le16(rom->image+0x18);
    atom_rom checked;
    if(!span(rom->size,pci,24) || !atom_rom_open(rom->image,rom->size,0x1002,(uint16_t)le16(rom->image+pci+6),&checked) ||
       checked.commands!=rom->commands || checked.data!=rom->data)return false;
    v->rom=checked;v->io=*io;v->scratch=scratch;v->scratch_bytes=bytes;v->step_limit=100000;v->error=ATOM_VM_OK;
    atom_table t;unsigned master=v->rom.data,entries=(le16(v->rom.image+master)-4)/2;
    if(entries<=23 || !le16(v->rom.image+master+4+23*2)){v->ready=true;return true;}
    if(!atom_rom_table(&v->rom,false,23,&t))return fail(v,ATOM_VM_TABLE);
    const uint8_t *b=t.bytes;unsigned pc=4;
    while(pc<t.size){
        if(b[pc]!=1 || !span(t.size,pc,2))return fail(v,ATOM_VM_BYTECODE);
        unsigned id=b[pc+1];pc+=2;
        if(v->indirect_begin[id])return fail(v,ATOM_VM_BYTECODE);
        unsigned start=pc;
        while(pc<t.size && b[pc]!=9){
            unsigned op=b[pc],n=op==0?1:op<=5?3:4;
            if(op==1 || op>8 || !span(t.size,pc,n))return fail(v,ATOM_VM_BYTECODE);
            if(op>=4){unsigned bits=b[pc+1],source=b[pc+2],dest=op<=5?source:b[pc+3];if(!bits || bits>32 || source>32-bits || dest>32-bits)return fail(v,ATOM_VM_BOUNDS);}
            pc+=n;
        }
        if(!span(t.size,pc,3) || b[pc]!=9)return fail(v,ATOM_VM_BYTECODE);
        unsigned base=(unsigned)(b-v->rom.image);
        if(base+pc>65535 || base+start>65535)return fail(v,ATOM_VM_BOUNDS);
        v->indirect_begin[id]=(uint16_t)(base+start);v->indirect_end[id]=(uint16_t)(base+pc);pc+=3;
    }
    v->ready=true;return true;
}
bool atom_vm_execute(atom_vm *v,unsigned command,uint32_t *ps,unsigned words){
    if(!v || v->busy)return false;
    if(!v->ready || !ps || !words || words>ATOM_VM_PARAMETERS || command>255){v->error=ATOM_VM_INPUT;return false;}
    v->busy=true;v->error=ATOM_VM_OK;v->steps=0;v->data_block=v->reg_block=v->fb_base=v->io_attr=v->bit_shift=v->io_mode=0;
    v->divmul[0]=v->divmul[1]=0;v->equal=v->above=false;v->delay_remaining_us=2000000;
    uint64_t now=v->io.time_us(v->io.context);v->deadline_us=now>UINT64_MAX-2000000?UINT64_MAX:now+2000000;
    bool ok=preflight(v,command,ps,words,0) && run(v,command,ps,words,0);
    v->busy=false;return ok;
}
const char *atom_vm_error_string(enum atom_vm_error error){
    switch(error){case ATOM_VM_OK:return "OK";case ATOM_VM_INPUT:return "invalid input";case ATOM_VM_TABLE:return "invalid/absent table";
        case ATOM_VM_BYTECODE:return "malformed bytecode";case ATOM_VM_UNSUPPORTED:return "unsupported firmware instruction";
        case ATOM_VM_BOUNDS:return "operand out of bounds";case ATOM_VM_IO:return "hardware transaction failed";case ATOM_VM_LIMIT:return "execution limit";
        default:return "unknown error";}
}
