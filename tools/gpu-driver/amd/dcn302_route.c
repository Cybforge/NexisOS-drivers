/* Original MIT-licensed Navi23 board-to-display-pipeline binding. */
#include "dcn302_route.h"
#include <string.h>
#define HG(v,f) (((v)&DCN302_HDMI_##f##_MASK)>>DCN302_HDMI_##f##_SHIFT)
static bool read(const dcn302_io *io,unsigned index,enum dcn302_hdmi_register r,uint32_t *v){return io->read(io->context,dcn302_hdmi_register_bytes[index][r],v);}
static bool hpd_index(const atom_board_path *p,unsigned *index){
    if(!p->has_hpd || p->hpd_gpio.index>UINT32_MAX/4 || p->hpd_gpio.shift>=32 ||
       p->hpd_gpio.index*4!=DCN302_GPIO_HPD_BYTES)return false;
    uint32_t bit=1u<<p->hpd_gpio.shift;
    for(unsigned i=0;i<5;i++)if(bit==dcn302_hpd_mask[i]){*index=i;return true;}
    return false;
}
static bool ddc_index(const atom_board_path *p,unsigned *index){
    if(!p->has_ddc || !p->ddc_hardware || p->ddc_slave || p->ddc_gpio.index>UINT32_MAX/4 || p->ddc_gpio.shift>=32)return false;
    uint32_t bit=1u<<p->ddc_gpio.shift;
    for(unsigned i=0;i<5;i++)if(p->ddc_gpio.index*4==dcn302_ddc_gpio_bytes[i] && bit==dcn302_ddc_clk_mask[i] && p->ddc_line==i){*index=i;return true;}
    return false;
}
enum dcn302_route_error dcn302_route_connected(const dcn302_io *io,const atom_board_path *p,unsigned hpd,bool *connected){
    if(connected)*connected=false;
    if(!io || !io->read || !p || !connected || hpd>=5 || p->hpd_state>1)return DCN302_ROUTE_INPUT;
    unsigned actual;if(!hpd_index(p,&actual) || actual!=hpd)return DCN302_ROUTE_UNSUPPORTED;
    uint32_t value;if(!io->read(io->context,dcn302_hpd_status_bytes[hpd],&value))return DCN302_ROUTE_IO;
    unsigned sense=(value&DCN302_HPD_SENSE_MASK)>>DCN302_HPD_SENSE_SHIFT;
    unsigned delayed=(value&DCN302_HPD_DELAYED_MASK)>>DCN302_HPD_DELAYED_SHIFT;
    if(sense!=delayed)return DCN302_ROUTE_CHANGED;
    *connected=sense==p->hpd_state;return DCN302_ROUTE_OK;
}
enum dcn302_route_error dcn302_route_find(const dcn302_io *io,const atom_board *board,unsigned width,unsigned height,dcn302_route *out){
    if(!out)return DCN302_ROUTE_INPUT;
    memset(out,0,sizeof(*out));
    if(!io || !io->read || !board || !board->count || board->count>ATOM_BOARD_MAX_PATHS || !width || !height)return DCN302_ROUTE_INPUT;
    dcn302_route result={0};unsigned active_links=0,active_otgs=0;uint32_t saved_be=0,saved_fe=0,saved_enable=0,saved_source=0;
    for(unsigned i=0;i<5;i++){
        uint32_t control;if(!io->read(io->context,dcn302_register_bytes[i][DCN302_R_CONTROL],&control))return DCN302_ROUTE_IO;
        if(control&DCN302_MASTER_ACTIVE_MASK){
            if(!(control&DCN302_MASTER_ENABLE_MASK))return DCN302_ROUTE_CHANGED;
            active_otgs++;
        }else if(control&DCN302_MASTER_ENABLE_MASK)return DCN302_ROUTE_CHANGED;
        uint32_t enable,be;if(!read(io,i,DCN302_HDMI_R_BE_ENABLE,&enable) || !read(io,i,DCN302_HDMI_R_BE,&be))return DCN302_ROUTE_IO;
        if(!HG(enable,LINK_ENABLE))continue;
        if(++active_links>1)return DCN302_ROUTE_AMBIGUOUS;
        if(!HG(enable,LINK_CLOCK))return DCN302_ROUTE_CHANGED;
        if(HG(be,LINK_MODE)!=3)return DCN302_ROUTE_UNSUPPORTED;
        unsigned mask=HG(be,FE_SOURCE),stream=0;
        if(!mask || mask>16 || (mask&(mask-1)))return DCN302_ROUTE_UNSUPPORTED;
        while((1u<<stream)!=mask)stream++;
        uint32_t fe;if(!read(io,stream,DCN302_HDMI_R_FE,&fe))return DCN302_ROUTE_IO;
        unsigned otg=HG(fe,PIPE),hpd=HG(be,HPD);
        if(otg>=5 || hpd>=5 || HG(fe,RGB_ENCODING) || HG(fe,COLOR_FORMAT))return DCN302_ROUTE_UNSUPPORTED;
        unsigned matches=0;
        for(unsigned path=0;path<board->count;path++){
            const atom_board_path *p=&board->paths[path];unsigned pin,bus;
            if(p->kind!=ATOM_BOARD_HDMI || !p->internal_phy || p->external_encoder || p->phy!=i)continue;
            if(!hpd_index(p,&pin) || pin!=hpd || !ddc_index(p,&bus))continue;
            if(++matches>1)return DCN302_ROUTE_AMBIGUOUS;
            result.path=path;result.ddc=bus;
        }
        if(!matches)return DCN302_ROUTE_UNSUPPORTED;
        const atom_board_path *p=&board->paths[result.path];bool connected;
        enum dcn302_route_error e=dcn302_route_connected(io,p,hpd,&connected);if(e)return e;
        if(!connected)return DCN302_ROUTE_ABSENT;
        bool active=false;
        if(!dcn302_otg_read_shape(io,otg,&result.shape,&active))return DCN302_ROUTE_UNSUPPORTED;
        if(!active)return DCN302_ROUTE_CHANGED;
        if(result.shape.hactive!=width || result.shape.vactive!=height)return DCN302_ROUTE_UNSUPPORTED;
        uint32_t source;if(!io->read(io->context,dcn302_register_bytes[otg][DCN302_R_SOURCE],&source))return DCN302_ROUTE_IO;
        unsigned opp=(source&DCN302_SEG0_MASK)>>DCN302_SEG0_SHIFT;if(opp>=5)return DCN302_ROUTE_UNSUPPORTED;
        result.link=i;result.stream=stream;result.otg=otg;result.opp=opp;result.hpd=hpd;
        result.max_tmds_khz=p->has_encoder_caps && (p->encoder_caps&ATOM_BOARD_HDMI6G)?600000:340000;
        saved_be=be;saved_enable=enable;saved_fe=fe;saved_source=source;
    }
    if(!active_links)return DCN302_ROUTE_ABSENT;
    if(active_otgs!=1)return active_otgs?DCN302_ROUTE_AMBIGUOUS:DCN302_ROUTE_CHANGED;
    uint32_t be,fe,enabled,source;
    if(!read(io,result.link,DCN302_HDMI_R_BE,&be) || !read(io,result.link,DCN302_HDMI_R_BE_ENABLE,&enabled) ||
       !read(io,result.stream,DCN302_HDMI_R_FE,&fe) || !io->read(io->context,dcn302_register_bytes[result.otg][DCN302_R_SOURCE],&source))return DCN302_ROUTE_IO;
    if((be^saved_be)&(DCN302_HDMI_FE_SOURCE_MASK|DCN302_HDMI_LINK_MODE_MASK|DCN302_HDMI_HPD_MASK) ||
       (fe^saved_fe)&(DCN302_HDMI_PIPE_MASK|DCN302_HDMI_RGB_ENCODING_MASK|DCN302_HDMI_COLOR_FORMAT_MASK) ||
       (enabled^saved_enable)&(DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK) || source!=saved_source)return DCN302_ROUTE_CHANGED;
    bool connected;enum dcn302_route_error e=dcn302_route_connected(io,&board->paths[result.path],result.hpd,&connected);
    if(e)return e;
    if(!connected)return DCN302_ROUTE_ABSENT;
    nexis_gpu_timing shape;bool active;
    if(!dcn302_otg_read_shape(io,result.otg,&shape,&active))return DCN302_ROUTE_CHANGED;
    if(!active || memcmp(&shape,&result.shape,sizeof(shape)))return DCN302_ROUTE_CHANGED;
    *out=result;return DCN302_ROUTE_OK;
}
