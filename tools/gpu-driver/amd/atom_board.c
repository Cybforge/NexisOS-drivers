/* Original MIT-licensed decoder of AMD ATOM board wiring. Layout reference:
 * Linux v6.12 atomfirmware.h, bios_parser2.c, ObjectID.h, link_factory.c.
 * No execution of option-ROM x86 code and no guessed GPIO/PHY assignments. */
#include "atom_board.h"
#include <string.h>
static unsigned u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool atom_board_phy(uint16_t encoder,unsigned *phy){
    if(!phy || (encoder>>12)!=2)return false;
    unsigned id=encoder&255,number=(encoder>>8)&15,base;
    switch(id){case 0x1e:base=0;break;case 0x20:base=2;break;case 0x21:base=4;break;case 0x25:base=6;break;default:return false;}
    if(number<1 || number>2 || (base==6 && number!=1))return false;
    *phy=base+number-1;return true;
}
/* gpio_pin_lut (Linux v6.12 atomfirmware.h, struct atom_gpio_pin_assignment): 4-byte table header, then 8-byte entries
 * { u32 data_a_reg_index (dword register index); u8 gpio_bitshift; u8 gpio_mask_bitshift; u8 gpio_id; u8 reserved }.
 * Linux counts the entries as (table size - header) / 8 and takes the FIRST entry with a matching gpio_id; so does this.
 * (A 6-byte layout was wrongly assumed for a while and made the first real RX 6600 stop at "board data", code 2.) */
#define GPIO_ENTRY_BYTES 8u
static enum atom_board_error gpio(const atom_table *table,uint8_t id,atom_board_gpio *out){
    atom_board_gpio item={0};bool found=false;
    for(unsigned off=4;off+GPIO_ENTRY_BYTES<=table->size;off+=GPIO_ENTRY_BYTES){
        const uint8_t *p=table->bytes+off;if(p[6]!=id)continue;
        item.index=u32(p);item.shift=p[4];item.mask_shift=p[5];item.id=id;found=true;break;
    }
    if(!found || !item.index || item.shift>=32 || item.mask_shift>=32)return ATOM_BOARD_TABLE;
    *out=item;return ATOM_BOARD_OK;
}
static enum atom_board_error records(const atom_table *table,unsigned start,unsigned minimum,
        const atom_table *pins,atom_board_path *path,bool wiring){
    if(!start)return ATOM_BOARD_OK;
    if(start<minimum || start>=table->size)return ATOM_BOARD_TABLE;
    unsigned seen=0;
    for(unsigned off=start;off<table->size;){
        if(table->size-off<2)return ATOM_BOARD_TABLE;
        const uint8_t *r=table->bytes+off;unsigned type=r[0],size=r[1];
        if(type==255 || !size)return ATOM_BOARD_OK;
        if(size<2 || size>table->size-off)return ATOM_BOARD_TABLE;
        unsigned flag=type==1?1:type==2?2:type==20?4:type==23?8:0;
        if(flag && (seen&flag))return ATOM_BOARD_AMBIGUOUS;
        seen|=flag;
        if((type==1 || type==2) && size<4)return ATOM_BOARD_TABLE;
        if((type==20 || type==23) && size<6)return ATOM_BOARD_TABLE;
        if(type==1 && wiring){
            if(path->has_ddc)return ATOM_BOARD_AMBIGUOUS;
            enum atom_board_error e=gpio(pins,r[2],&path->ddc_gpio);if(e)return e;
            path->has_ddc=true;path->ddc_hardware=(r[2]&128)!=0;
            path->ddc_line=r[2]&15;path->ddc_engine=(r[2]>>4)&7;path->ddc_slave=r[3];
        }
        if(type==2 && wiring){
            if(path->has_hpd || r[2]&128 || r[3]>1)return path->has_hpd?ATOM_BOARD_AMBIGUOUS:ATOM_BOARD_TABLE;
            enum atom_board_error e=gpio(pins,r[2],&path->hpd_gpio);if(e)return e;
            path->has_hpd=true;path->hpd_state=r[3];
        }
        if(type==20){
            if(path->has_encoder_caps)return ATOM_BOARD_AMBIGUOUS;
            path->encoder_caps=u32(r+2);path->has_encoder_caps=true;
        }
        if(type==23)path->connector_caps=u32(r+2);
        off+=size;
    }
    return ATOM_BOARD_TABLE; /* Every chain must end explicitly. */
}
enum atom_board_error atom_board_open(const atom_rom *rom,atom_board *out){
    if(!out)return ATOM_BOARD_INPUT;
    memset(out,0,sizeof(*out));
    if(!rom || !rom->image)return ATOM_BOARD_INPUT;
    atom_table objects,pins,dce;
    if(!atom_rom_table(rom,false,22,&objects) || !atom_rom_table(rom,false,12,&pins) ||
       !atom_rom_table(rom,false,27,&dce))return ATOM_BOARD_TABLE;
    if(objects.format!=1 || (objects.revision!=4 && objects.revision!=5) ||
       pins.format!=2 || pins.revision!=1 || dce.format!=4 || dce.revision<1 || dce.revision>5)return ATOM_BOARD_VERSION;
    if(objects.size<8 || pins.size<4+GPIO_ENTRY_BYTES || dce.size<48)return ATOM_BOARD_TABLE;
    unsigned count=objects.bytes[6],minimum=8+count*16;
    if(!count || count>ATOM_BOARD_MAX_PATHS || minimum>objects.size)return ATOM_BOARD_TABLE;
    atom_board b={0};const uint8_t *c=dce.bytes;
    b.reference_khz=u16(c+12)*10;b.i2c_reference_khz=u16(c+14)*10;
    b.phy_reference_khz=u16(c+36)*10;b.boot_display_khz=u32(c+8)>UINT32_MAX/10?0:u32(c+8)*10;
    /* Only dce_refclk is used (HUBBUB / DDC timing). The other dce_info numbers (pipes, phys, plls, aux, i2c and phy
     * reference, boot clock) are decoded for the log but never demanded: Linux does not range-check them either. */
    if(!b.reference_khz || b.reference_khz>200000)return ATOM_BOARD_TABLE;
    b.pipes=c[42];b.plls=c[44];b.phys=c[45];b.aux=c[46];b.devices=(uint16_t)u16(objects.bytes+4);
    enum atom_board_error first=ATOM_BOARD_OK;
    for(unsigned i=0;i<count;i++){
        const uint8_t *p=objects.bytes+8+i*16;uint16_t id=(uint16_t)u16(p);
        if((id>>12)!=3)continue; /* Other object types can describe non-port paths. */
        unsigned number=(id>>8)&15;
        atom_board_path item={0};item.connector=id;item.kind=id&255;item.encoder=(uint16_t)u16(p+4);
        item.device_tag=(uint16_t)u16(p+12);item.external_encoder=objects.revision==4?(uint16_t)u16(p+6):0;
        unsigned phy=0;item.internal_phy=atom_board_phy(item.encoder,&phy);item.phy=(uint8_t)phy;
        /* Linux looks every connector up on its own, so a malformed DisplayPort/other record never affects the HDMI
         * port. Same here: such a path is skipped; only a defective HDMI path (the only one this driver can use) fails. */
        enum atom_board_error e=ATOM_BOARD_OK;
        if(!number || number>6)e=ATOM_BOARD_TABLE;
        for(unsigned n=0;!e && n<b.count;n++)if(b.paths[n].connector==id)e=ATOM_BOARD_AMBIGUOUS;
        if(!e)e=records(&objects,u16(p+2),minimum,&pins,&item,true);
        if(!e && objects.revision==4){
            e=records(&objects,u16(p+8),minimum,&pins,&item,false);
            /* An external encoder is not safe for native takeover. Decode its
             * extent, without replacing the internal encoder's capabilities. */
            atom_board_path external={0};
            if(!e)e=records(&objects,u16(p+10),minimum,&pins,&external,false);
        }
        if(e){if(item.kind==ATOM_BOARD_HDMI)return e;if(!first)first=e;continue;}
        b.paths[b.count++]=item;
    }
    if(!b.count)return first?first:ATOM_BOARD_TABLE;
    *out=b;return ATOM_BOARD_OK;
}
