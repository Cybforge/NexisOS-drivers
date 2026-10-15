#include "rx6600.h"
#include "../common/nxlog.h"
#include "../common/cta_audio.h"
#include <string.h>
static bool read_reg(void *context,uint32_t offset,uint32_t *value){
    rx6600_state *s=context;return s->services->read32(s->services->service_context,5,offset,value);
}
static bool write_reg(void *context,uint32_t offset,uint32_t value){
    rx6600_state *s=context;return s->services->write32(s->services->service_context,5,offset,value);
}
static bool delay(void *context,uint32_t us){rx6600_state *s=context;return s->services->delay_us(s->services->service_context,us);}
static uint64_t now(void *context){rx6600_state *s=context;return s->services->time_us(s->services->service_context);}
static enum rx6600_error fail(rx6600_state *s,enum rx6600_error error){s->ready=false;s->error=error;return error;}
static bool resources(rx6600_state *);
static void edid_report(rx6600_state *);
static bool sink_read_ddc(void *context,uint8_t address,uint8_t offset,uint8_t *value){return dcn302_ddc_read_byte(&((rx6600_state *)context)->ddc,address,offset,value);}
static bool sink_write_ddc(void *context,uint8_t address,uint8_t offset,uint8_t value){return dcn302_ddc_write_byte(&((rx6600_state *)context)->ddc,address,offset,value);}
static bool NEXIS_GPU_CALL clock_floor(void *context,enum dcn302_smu_clock clock,uint32_t mhz,uint32_t *out){
    if(!out)return false;
    *out=0;rx6600_state *s=context;
    if(!s || !s->services || !s->ready || s->busy)return false;
    if(s->firmware_changed || s->firmware_poisoned)return false;
    if(!resources(s)){fail(s,RX6600_RESOURCE);return false;}
    /* PSTATE_ALLOW is forced low by this fixed-floor model. Even an upward
     * UCLK request can require a transition; defer every such command until
     * the old policy is restored, not just requests below the floor. */
    if(clock==DCN302_SMU_UCLK && (s->hubbub_transaction.dirty || s->hubbub_transaction.applied || s->hubbub_transaction.poisoned ||
       s->dpp_transaction.dirty || s->dpp_transaction.applied || s->dpp_transaction.poisoned ||
       s->timing_transaction.dirty || s->timing_transaction.applied || s->timing_transaction.poisoned))return false;
    if(s->hubp_transaction.dirty || s->hubp_transaction.applied || s->hubp_transaction.poisoned ||
       s->hubbub_transaction.dirty || s->hubbub_transaction.applied || s->hubbub_transaction.poisoned ||
       s->dpp_transaction.dirty || s->dpp_transaction.applied || s->dpp_transaction.poisoned){
        const unsigned clocks[]={DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_SOCCLK,DCN302_SMU_PHYCLK};
        for(unsigned n=0;n<4;n++)if((unsigned)clock==clocks[n] && mhz<s->hubp_floor_mhz[n])return false;
    }
    /* Never lower the voltage floor underneath programmed native clocks or
     * the old/new clocks still needed for an outstanding rollback. */
    if(clock==DCN302_SMU_DISPCLK || clock==DCN302_SMU_DPPCLK){
        uint32_t minimum=clock==DCN302_SMU_DISPCLK?s->dfs.disp_floor_mhz:s->dfs.dpp_floor_mhz;
        if(!s->dfs.valid || mhz<minimum)return false;
        if(s->dfs_transaction.dirty && mhz<(clock==DCN302_SMU_DISPCLK?s->dfs_transaction.required_disp_floor_mhz:s->dfs_transaction.required_dpp_floor_mhz))return false;
    }
    s->busy=true;bool ok=dcn302_smu_set_floor(&s->smu,clock,mhz,out);
    if(!ok && s->smu.poisoned)fail(s,RX6600_SMU);
    s->busy=false;return ok;
}
static bool resources(rx6600_state *s){
    nexis_gpu_resource v,r;const nexis_gpu_services *k=s->services;
    return k->resource(k->service_context,0,&v) && k->resource(k->service_context,5,&r) &&
        !memcmp(&s->vram,&v,sizeof(v)) && !memcmp(&s->registers,&r,sizeof(r));
}
static bool NEXIS_GPU_CALL display_clocks(void *context,const dcn302_dfs_request *r,enum rx6600_dfs_operation op){
    rx6600_state *s=context;
    if(!s || !s->services || s->busy || (!s->ready && op!=RX6600_DFS_RESTORE) || (unsigned)op>RX6600_DFS_RESTORE ||
       (op==RX6600_DFS_PREPARE?!r:r!=NULL))return false;
    if(s->firmware_changed || s->firmware_poisoned)return false;
    if(op==RX6600_DFS_APPLY && (s->dfs_transaction.dirty || s->dfs_transaction.applied || s->dfs_transaction.poisoned))return false;
    /* RQ/DLG and watermarks are bound to these exact clocks. Restore them
     * before changing clocks, including a DFS rollback. */
    if(op!=RX6600_DFS_PREPARE && (s->timing_transaction.dirty || s->timing_transaction.applied || s->timing_transaction.poisoned ||
       s->dpp_transaction.dirty || s->dpp_transaction.applied || s->dpp_transaction.poisoned ||
       s->hubp_transaction.dirty || s->hubp_transaction.applied ||
       s->hubbub_transaction.dirty || s->hubbub_transaction.applied))return false;
    s->busy=true;enum dcn302_dfs_error error=DCN302_DFS_OK;
    if(!resources(s)){fail(s,RX6600_RESOURCE);s->busy=false;return false;}
    if(op==RX6600_DFS_PREPARE){
        if(s->dfs_transaction.dirty || s->dfs_transaction.poisoned)error=DCN302_DFS_BUSY;
        else{
            error=dcn302_dfs_prepare(&s->io,r,&s->dfs_transaction);
            if(!error && memcmp(&s->dfs_transaction.before,&s->dfs,sizeof(s->dfs)))error=DCN302_DFS_READBACK;
            if(error)s->dfs_transaction.prepared=false;
        }
    }else if(op==RX6600_DFS_APPLY)error=dcn302_dfs_apply_disabled(&s->io,now,&s->smu,&s->dfs_transaction);
    else error=dcn302_dfs_restore_disabled(&s->io,now,&s->smu,&s->dfs_transaction);
    if(op!=RX6600_DFS_PREPARE && !error)error=dcn302_dfs_read(&s->io,&s->dfs);
    if(s->dfs_transaction.poisoned || (error && op!=RX6600_DFS_PREPARE && s->dfs_transaction.dirty))fail(s,RX6600_CLOCK);
    s->busy=false;return error==DCN302_DFS_OK;
}
static bool fixed_rate(rx6600_state *s){
    const enum dcn302_register registers[]={DCN302_R_V_CONTROL,DCN302_R_V_MIN,DCN302_R_V_MAX};
    for(unsigned n=0;n<3;n++){
        uint32_t value;
        if(!s->io.read(s->io.context,dcn302_register_bytes[s->route.otg][registers[n]],&value) || value!=s->fixed_rate[n])return false;
    }
    return true;
}
static enum rx6600_error prove(rx6600_state *s){
    const nexis_gpu_services *k=s->services;dcn302_route route;dcn302_surface surface;
    if(!resources(s))return RX6600_RESOURCE;
    if(!dcn302_smu_usable(&s->smu))return RX6600_SMU;
    dcn302_dfs_snapshot dfs;
    if(dcn302_dfs_read(&s->io,&dfs) || memcmp(&s->dfs,&dfs,sizeof(dfs)))return RX6600_CLOCK;
    dcn302_reference ref;
    if(dcn302_reference_read(&s->io,s->board.reference_khz,&ref) || !dcn302_reference_equal(&ref,&s->reference))return RX6600_CLOCK;
    if(!fixed_rate(s))return RX6600_CLOCK;
    if(dcn302_route_find(&s->io,&s->board,k->width,k->height,&route)!=DCN302_ROUTE_OK)return RX6600_ROUTE;
    /* Identity and complete geometry, including OPP routing, remain stable. */
    if(memcmp(&s->route,&route,sizeof(route)))return RX6600_CHANGED;
    if(dcn302_surface_bind(&s->io,&route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&surface)!=DCN302_SURFACE_OK)return RX6600_SURFACE;
    if(memcmp(&s->surface,&surface,sizeof(surface)))return RX6600_CHANGED;
    return RX6600_OK;
}
static bool NEXIS_GPU_CALL bandwidth_plan(void *context,const nexis_gpu_timing *timing,bool prepared,dcn302_dml_output *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));rx6600_state *s=context;
    if(!s || !timing || !s->ready || s->busy || s->dfs_transaction.dirty || s->dfs_transaction.poisoned ||
       timing->hactive!=s->route.shape.hactive || timing->vactive!=s->route.shape.vactive)return false;
    if(s->firmware_changed || s->firmware_poisoned)return false;
    s->busy=true;s->error=RX6600_BANDWIDTH;bool ok=false;enum rx6600_error error=prove(s);
    if(error){fail(s,error);goto done;}
    if(prepared && (!s->dfs_transaction.prepared || s->dfs_transaction.applied ||
       !s->dfs_transaction.after.valid || memcmp(&s->dfs_transaction.before,&s->dfs,sizeof(s->dfs))))goto done;
    /* Require owned acknowledged UCLK/DCF/SOC/PHY floors; DPM minima alone
     * cannot guarantee the model's bandwidth after firmware takeover. */
    const unsigned clocks[]={DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_SOCCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<4;n++)if(!s->smu.floor_known[clocks[n]] || !s->smu.floor_mhz[clocks[n]])goto done;
    const dcn302_dfs_snapshot *dfs=prepared?&s->dfs_transaction.after:&s->dfs;
    dcn302_dml_job *j=&s->dml_job;memset(j,0,sizeof(*j));j->workspace=&s->dml_workspace;
    dcn302_dml_input *i=&j->input;i->timing=*timing;i->pitch_pixels=s->surface.pitch;i->pipe=s->route.otg;
    i->channels=s->memory.channels;i->channel_bytes=s->memory.channel_bytes;
    i->dram_mts=(uint32_t)s->smu.floor_mhz[DCN302_SMU_UCLK]*16u;
    i->dcf_khz=(uint32_t)s->smu.floor_mhz[DCN302_SMU_DCEFCLK]*1000u;
    i->soc_khz=(uint32_t)s->smu.floor_mhz[DCN302_SMU_SOCCLK]*1000u;
    /* DCN302's discrete-GDDR6 bounding-box fabric clock uses DCFCLK, per
     * AMD dcn302_fpu.c. It is not a fabricated measured FCLK. */
    i->fabric_khz=i->dcf_khz;i->phy_khz=(uint32_t)s->smu.floor_mhz[DCN302_SMU_PHYCLK]*1000u;
    i->disp_khz=dfs->disp_khz;i->dpp_khz=dfs->pipe_khz[s->surface.hubp];i->ref_khz=s->reference.khz;
    i->vco_khz_q32=dfs->vco_q32;
    unsigned line;enum nexis_dml_scope_result scope=dcn302_dml_calculate(j,&line);
    if(scope!=NEXIS_DML_SCOPE_OK || j->error!=DCN302_DML_OK){s->error=RX6600_BANDWIDTH;goto done;}
    *out=j->output;ok=true;s->error=RX6600_OK;
done:
    s->busy=false;return ok;
}
static bool dpp_guard(void *context){
    rx6600_state *s=context;dcn302_dfs_snapshot current;
    if(!s || !s->services || !resources(s) || !dcn302_smu_usable(&s->smu) || s->smu.busy ||
       dcn302_dfs_read(&s->io,&current) || memcmp(&current,&s->hubp_clock_target,sizeof(current)))return false;
    const unsigned clocks[]={DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_SOCCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<4;n++)if(!s->hubp_floor_mhz[n] || !s->smu.floor_known[clocks[n]] || s->smu.floor_mhz[clocks[n]]<s->hubp_floor_mhz[n])return false;
    if(!s->smu.floor_known[DCN302_SMU_DISPCLK] || !s->smu.floor_known[DCN302_SMU_DPPCLK] ||
       s->smu.floor_mhz[DCN302_SMU_DISPCLK]<s->hubp_clock_target.disp_floor_mhz ||
       s->smu.floor_mhz[DCN302_SMU_DPPCLK]<s->hubp_clock_target.dpp_floor_mhz)return false;
    return !dcn302_hubp_verify_installed(&s->io,&s->hubp_transaction) &&
        !dcn302_hubbub_verify_installed(&s->io,&s->hubbub_transaction);
}
static bool timing_guard(void *context){
    rx6600_state *s=context;
    return dpp_guard(context) && !dcn302_dpp_verify_installed(&s->io,&s->dpp_transaction);
}
static bool firmware_fault(rx6600_state *s,enum atom_vm_error e){
    if(s->firmware_error==ATOM_VM_OK)s->firmware_error=e;
    /* Even an index/data-port write may have posted before a reported error.
     * Never reset or blindly repeat an uncertain hardware command. */
    if(s->firmware_changed)s->firmware_poisoned=true;
    return false;
}
/* Baseline = every transaction is back at (or was never moved from) the original hardware state. The guarded
 * firmware path then only needs the parent proofs that do not depend on installed new-mode registers. */
bool rx6600_baseline_clean(rx6600_state *s){
    if(!s || !s->services || !resources(s) || !dcn302_smu_usable(&s->smu) || s->smu.busy)return false;
    if(s->hubp_transaction.dirty || s->hubp_transaction.applied || s->hubp_transaction.poisoned ||
       s->hubbub_transaction.dirty || s->hubbub_transaction.applied || s->hubbub_transaction.poisoned ||
       s->dpp_transaction.dirty || s->dpp_transaction.applied || s->dpp_transaction.poisoned ||
       s->timing_transaction.dirty || s->timing_transaction.applied || s->timing_transaction.poisoned ||
       s->dfs_transaction.dirty || s->dfs_transaction.applied || s->dfs_transaction.poisoned ||
       s->resync.dirty || s->resync.applied || s->resync.poisoned)return false;
    dcn302_dfs_snapshot dfs;dcn302_reference ref;
    return !dcn302_dfs_read(&s->io,&dfs) && !memcmp(&dfs,&s->dfs,sizeof(dfs)) &&
        !dcn302_reference_read(&s->io,s->board.reference_khz,&ref) && dcn302_reference_equal(&ref,&s->reference);
}
static bool firmware_guard(rx6600_state *s){
    if(s->firmware_poisoned || !(s->fw_baseline?rx6600_baseline_clean(s):timing_guard(s)))return false;
    for(unsigned sweep=0;sweep<2;sweep++){
        for(unsigned p=0;p<5;p++){
            uint32_t c,k,v;
            if(!s->io.read(s,dcn302_register_bytes[p][DCN302_R_CONTROL],&c) ||
               !s->io.read(s,dcn302_register_bytes[p][DCN302_R_CLOCK],&k) ||
               !s->io.read(s,dcn302_register_bytes[p][DCN302_R_VTG],&v) ||
               (c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) ||
               (k&DCN302_BUSY_MASK) || (v&DCN302_VTG_ENABLE_MASK))return false;
        }
        bool connected=false;
        if(dcn302_route_connected(&s->io,&s->board.paths[s->route.path],s->route.hpd,&connected) || !connected)return false;
    }
    return true;
}
static bool firmware_offset(rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t *offset){
    /* AMD cail_reg_* -> RREG32/WREG32 uses dword indices. The VM itself
     * handles direct REG0's value<<2 rule; do not shift it again here.
     * IIO writes, including index/data ports, use the same native mapping.
     * Legacy PLL/MC operands have no native Navi23 port mapping. */
    if(space!=ATOM_VM_MMIO)return firmware_fault(s,ATOM_VM_UNSUPPORTED);
    uint64_t bytes=(uint64_t)index*4;
    if(bytes>UINT32_MAX || bytes>s->registers.bytes || s->registers.bytes-bytes<4)return firmware_fault(s,ATOM_VM_BOUNDS);
    *offset=(uint32_t)bytes;return true;
}
static bool firmware_read(void *context,enum atom_vm_space space,uint32_t index,uint32_t *out){
    rx6600_state *s=context;uint32_t offset,value;
    if(!out)return firmware_fault(s,ATOM_VM_INPUT);
    *out=0;
    /* Linux amdgpu_atombios.c: cail_pll_read() and cail_mc_read() "return 0;" - the ATOM PLL/MC spaces have no
     * hardware behind them on amdgpu ASICs, so a VBIOS table touching them reads 0 instead of aborting. */
    if(space==ATOM_VM_PLL || space==ATOM_VM_MC)return true;
    if(!firmware_offset(s,space,index,&offset))return false;
    if(!firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    if(!s->io.read(s,offset,&value))return firmware_fault(s,ATOM_VM_IO);
    if(!firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    *out=value;return true;
}
static bool firmware_write(void *context,enum atom_vm_space space,uint32_t index,uint32_t value){
    rx6600_state *s=context;uint32_t offset;
    if(space==ATOM_VM_PLL || space==ATOM_VM_MC)return true;   /* Linux cail_pll_write()/cail_mc_write() are empty */
    if(!firmware_offset(s,space,index,&offset))return false;
    if(!firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    /* Pixel/PHY/stream setup must not start any scanout before the parent
     * has completed link/clock/audio validation and the final activation. */
    for(unsigned p=0;p<5;p++)if((offset==dcn302_register_bytes[p][DCN302_R_CONTROL] && (value&DCN302_MASTER_ENABLE_MASK)) ||
       (offset==dcn302_register_bytes[p][DCN302_R_VTG] && (value&DCN302_VTG_ENABLE_MASK)))return firmware_fault(s,ATOM_VM_UNSUPPORTED);
    s->firmware_changed=true;
    if(!s->io.write(s,offset,value))return firmware_fault(s,ATOM_VM_IO);
    return firmware_guard(s)?true:firmware_fault(s,ATOM_VM_IO);
}
static bool firmware_delay(void *context,uint32_t us){
    rx6600_state *s=context;
    if(us>2000000)return firmware_fault(s,ATOM_VM_LIMIT);
    if(!firmware_guard(s) || !s->io.delay_us(s,us) || !firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    return true;
}
static bool vm_ensure(rx6600_state *s){
    if(s->firmware_vm.ready)return true;
    atom_vm_io io;volatile atom_vm_io *operations=&io;
    operations->context=s;operations->read=firmware_read;operations->write=firmware_write;operations->delay_us=firmware_delay;operations->time_us=now;
    return atom_vm_init(&s->firmware_vm,&s->firmware_rom,&io,s->firmware_scratch,sizeof(s->firmware_scratch));
}
static void dump_hex(const char *name,const uint8_t *p,unsigned n);
/* ---- video BIOS command list diagnostics: log only, no hardware access ---- */
static unsigned rom_u16(const atom_rom *r,unsigned at){return at<=r->size && r->size-at>=2?(unsigned)r->image[at]|(unsigned)r->image[at+1]<<8:0;}
static void rom_report(const atom_rom *rom){
    if(!nx_log_enabled() || !rom || !rom->image)return;
    const unsigned list=rom->commands,bytes=rom_u16(rom,list),entries=bytes>=4?(bytes-4)/2:0;
    unsigned set=0;
    for(unsigned i=0;i<entries;i++)if(rom_u16(rom,list+4+i*2))set++;
    nx_logf("rx6600: video BIOS: %u bytes from the ATOM image on, ATOM header 0x%x, command list 0x%x (%u bytes = %u entries, %u set), data list 0x%x%s",
        (unsigned)rom->size,rom_u16(rom,0x48),list,bytes,entries,set,(unsigned)rom->data,rom->checksum_ok?"":", checksum not zero");
    nx_logf("rx6600: video BIOS command entries: [0]=%x [4]=%x [12]=%x [13]=%x [42]=%x [46]=%x [50]=%x [76]=%x",
        rom_u16(rom,list+4),rom_u16(rom,list+4+4*2),rom_u16(rom,list+4+12*2),rom_u16(rom,list+4+13*2),
        rom_u16(rom,list+4+42*2),rom_u16(rom,list+4+46*2),rom_u16(rom,list+4+50*2),rom_u16(rom,list+4+76*2));
    if(bytes>=4 && list+bytes<=rom->size)dump_hex("command list",rom->image+list,bytes<176?bytes:176);
}
/* Looks through the whole ROM for tables that have the header of the three display commands (format 1, revision 7 / 6 / 5, a plausible
 * size, an end-of-table opcode at the end) and says whether the command list points at them: shows a table that exists but is
 * listed under another number, or not at all. */
static void rom_scan(const atom_rom *rom){
    if(!nx_log_enabled() || !rom || !rom->image)return;
    unsigned found=0;
    for(unsigned o=0;o+8<=rom->size && found<8;o++){
        const uint8_t *p=rom->image+o;
        const unsigned size=p[0]|(unsigned)p[1]<<8,revision=p[3];
        if(size<24 || size>0x4000 || o+size>rom->size || p[2]!=1 || (revision!=5 && revision!=6 && revision!=7) || (p[5]&127)>80)continue;
        bool eot=false;
        for(unsigned k=size>10?size-10:6;k<size;k++)if(p[k]==0x5b)eot=true;
        if(!eot)continue;
        unsigned listed=0xffff;
        const unsigned list=rom->commands,bytes=rom_u16(rom,list),entries=bytes>=4?(bytes-4)/2:0;
        for(unsigned i=0;i<entries && listed==0xffff;i++)if(rom_u16(rom,list+4+i*2)==o)listed=i;
        found++;
        if(listed==0xffff)nx_logf("rx6600: video BIOS: table-like header (format 1, revision %u) at 0x%x, %u bytes, workspace %u, parameters %u: NOT in the command list",
            revision,o,size,(unsigned)p[4],(unsigned)(p[5]&127));
        else nx_logf("rx6600: video BIOS: table-like header (format 1, revision %u) at 0x%x, %u bytes, workspace %u, parameters %u: command list entry %u",
            revision,o,size,(unsigned)p[4],(unsigned)(p[5]&127),listed);
    }
    if(!found)nx_logf("rx6600: video BIOS: no table with the header of SetPixelClock / transmitter / encoder control anywhere in the %u bytes",(unsigned)rom->size);
}
/* Before the screen goes dark: can the interpreter run the two video BIOS command tables of the switch? Prints their versions (so the next
 * log shows what the card really has) and structurally checks every reachable instruction, without touching the hardware. */
static bool NEXIS_GPU_CALL firmware_check(void *context){
    rx6600_state *s=context;
    if(!s || !s->services || s->busy || s->firmware_poisoned)return false;
    atom_table pixel,transmitter;
    const bool have_pixel=atom_rom_table(&s->firmware_rom,true,ATOM_DISPLAY_PIXEL_CLOCK,&pixel),have_transmitter=atom_rom_table(&s->firmware_rom,true,ATOM_DISPLAY_TRANSMITTER,&transmitter);
    nx_logf("rx6600: video BIOS command tables: SetPixelClock %s v%u.%u (%u bytes), UNIPHY transmitter %s v%u.%u (%u bytes)",
        have_pixel?"found":"MISSING",have_pixel?(unsigned)pixel.format:0,have_pixel?(unsigned)pixel.revision:0,have_pixel?(unsigned)pixel.size:0,
        have_transmitter?"found":"MISSING",have_transmitter?(unsigned)transmitter.format:0,have_transmitter?(unsigned)transmitter.revision:0,have_transmitter?(unsigned)transmitter.size:0);
    if(!have_pixel || !have_transmitter){rom_report(&s->firmware_rom);rom_scan(&s->firmware_rom);return false;}
    s->busy=true;
    bool ok=vm_ensure(s);
    enum atom_vm_error e1=ATOM_VM_INPUT,e2=ATOM_VM_INPUT;
    unsigned f1[3]={0,0,0},f2[3]={0,0,0};
    if(ok){
        e1=atom_display_check(&s->firmware_vm,ATOM_DISPLAY_PIXEL_CLOCK);
        if(s->firmware_vm.fault_set){f1[0]=s->firmware_vm.fault_command;f1[1]=s->firmware_vm.fault_pc;f1[2]=s->firmware_vm.fault_opcode;}
        e2=atom_display_check(&s->firmware_vm,ATOM_DISPLAY_TRANSMITTER);
        if(s->firmware_vm.fault_set){f2[0]=s->firmware_vm.fault_command;f2[1]=s->firmware_vm.fault_pc;f2[2]=s->firmware_vm.fault_opcode;}
    }
    s->busy=false;
    if(!ok){nx_logf("rx6600: firmware interpreter could not be set up (ATOM error %u)",(unsigned)s->firmware_vm.error);return false;}
    nx_logf("rx6600: firmware preflight: SetPixelClock %s (ATOM error %u), transmitter %s (ATOM error %u)",e1?"REFUSED":"ok",(unsigned)e1,e2?"REFUSED":"ok",(unsigned)e2);
    if(e1)nx_logf("rx6600: firmware preflight: SetPixelClock stopped in command table %u at byte 0x%x, opcode 0x%x",f1[0],f1[1],f1[2]);
    if(e2)nx_logf("rx6600: firmware preflight: transmitter stopped in command table %u at byte 0x%x, opcode 0x%x",f2[0],f2[1],f2[2]);
    return !e1 && !e2;
}
static enum atom_vm_error NEXIS_GPU_CALL firmware_command(void *context,enum atom_display_command command,uint32_t khz,unsigned action){
    rx6600_state *s=context;
    if(!s || !s->services || !s->ready || s->busy || s->firmware_poisoned || !(s->fw_baseline?true:s->dpp_transaction.applied) ||
       s->route.path>=s->board.count || (khz!=s->clock.pixel_khz && (s->fw_baseline || khz!=s->timing_transaction.timing.pixel_khz)))return ATOM_VM_INPUT;
    uint8_t parameters[60];unsigned bytes=0;const atom_board_path *path=&s->board.paths[s->route.path];
    switch(command){
        case ATOM_DISPLAY_PIXEL_CLOCK:
            if(action || !atom_hdmi_pixel_parameters(parameters,path,s->route.otg,khz))return ATOM_VM_INPUT;
            bytes=16;break;
        case ATOM_DISPLAY_ENCODER:
            if(action || !atom_hdmi_stream_parameters(parameters,s->route.stream,khz))return ATOM_VM_INPUT;
            bytes=12;break;
        case ATOM_DISPLAY_TRANSMITTER:{
            atom_table table;if(!atom_rom_table(&s->firmware_rom,true,command,&table))return ATOM_VM_TABLE;
            if(table.format!=1 || (table.revision!=6 && table.revision!=7))return ATOM_VM_UNSUPPORTED;
            bytes=table.revision==6?32:60;
            if(!(table.revision==6?atom_hdmi_transmitter_parameters(parameters,path,s->route.stream,s->route.hpd,khz,action):
                 atom_hdmi_transmitter_v7_parameters(parameters,path,s->route.stream,s->route.hpd,khz,action)))return ATOM_VM_INPUT;
            break;
        }
        default:return ATOM_VM_UNSUPPORTED;
    }
    s->busy=true;s->firmware_error=ATOM_VM_OK;
    if(!vm_ensure(s))s->firmware_error=s->firmware_vm.error;
    if(s->firmware_error==ATOM_VM_OK && !firmware_guard(s))firmware_fault(s,ATOM_VM_IO);
    enum atom_vm_error e=s->firmware_error;
    if(e==ATOM_VM_OK)e=atom_display_execute(&s->firmware_vm,command,parameters,bytes);
    if(s->firmware_error!=ATOM_VM_OK)e=s->firmware_error;
    if(e==ATOM_VM_OK && !firmware_guard(s)){firmware_fault(s,ATOM_VM_IO);e=s->firmware_error;}
    s->firmware_error=e;
    if(e && s->firmware_changed){s->firmware_poisoned=true;fail(s,RX6600_CLOCK);}
    if(!e){memset(s->firmware_parameters,0,sizeof(s->firmware_parameters));memcpy(s->firmware_parameters,parameters,bytes);s->firmware_parameter_bytes=bytes;}
    s->busy=false;return e;
}
static bool NEXIS_GPU_CALL dpp_registers(void *context,enum rx6600_dpp_operation op){
    rx6600_state *s=context;
    if(!s || !s->services || s->busy || (unsigned)op>RX6600_DPP_RESTORE ||
       (!s->ready && op!=RX6600_DPP_RESTORE) || !s->dpp_transaction.prepared ||
       s->timing_transaction.dirty || s->timing_transaction.applied || s->timing_transaction.poisoned)return false;
    if(s->firmware_changed || s->firmware_poisoned)return false;
    if(op==RX6600_DPP_APPLY && (s->dpp_transaction.dirty || s->dpp_transaction.applied || s->dpp_transaction.poisoned))return false;
    s->busy=true;
    enum dcn302_error e=op==RX6600_DPP_APPLY?dcn302_dpp_apply_disabled(&s->io,&s->dpp_transaction):
        dcn302_dpp_restore_disabled(&s->io,&s->dpp_transaction);
    if(s->dpp_transaction.poisoned || (e && s->dpp_transaction.dirty))fail(s,RX6600_CLOCK);
    else s->error=e?RX6600_CLOCK:RX6600_OK;
    s->busy=false;return !e;
}
static bool NEXIS_GPU_CALL timing_registers(void *context,enum rx6600_timing_operation op){
    rx6600_state *s=context;
    if(!s || !s->services || s->busy || (unsigned)op>RX6600_TIMING_RESTORE ||
       (!s->ready && op!=RX6600_TIMING_RESTORE) || !s->timing_transaction.prepared)return false;
    if(op==RX6600_TIMING_RESTORE && (s->firmware_changed || s->firmware_poisoned))return false;
    if(op==RX6600_TIMING_APPLY && (s->timing_transaction.dirty || s->timing_transaction.applied || s->timing_transaction.poisoned))return false;
    s->busy=true;
    enum dcn302_error e=op==RX6600_TIMING_APPLY?dcn302_timing_apply_disabled(&s->io,&s->timing_transaction):
        dcn302_timing_restore_disabled(&s->io,&s->timing_transaction);
    if(s->timing_transaction.poisoned || (e && s->timing_transaction.dirty))fail(s,RX6600_CLOCK);
    else s->error=e?RX6600_CLOCK:RX6600_OK;
    s->busy=false;return !e;
}
static bool NEXIS_GPU_CALL bandwidth_registers(void *context,const nexis_gpu_timing *timing,bool prepared,enum rx6600_hubp_operation op){
    rx6600_state *s=context;
    if(!s || !s->services || s->busy || (unsigned)op>RX6600_HUBP_CANCEL ||
       (!s->ready && op!=RX6600_HUBP_RESTORE) ||
       (op==RX6600_HUBP_PREPARE?!timing:(timing!=NULL || prepared)))return false;
    if(s->firmware_changed || s->firmware_poisoned)return false;
    dcn302_hubp_transaction *t=&s->hubp_transaction;
    dcn302_hubbub_transaction *w=&s->hubbub_transaction;
    dcn302_timing_transaction *q=&s->timing_transaction;
    dcn302_dpp_transaction *d=&s->dpp_transaction;
    /* Native old timing/scaler/cursors must be back before fetch or forced
     * policy is released, including poison with uncertain posted writes. */
    if(q->dirty || q->applied || q->poisoned || d->dirty || d->applied || d->poisoned)return false;
    if(op==RX6600_HUBP_CANCEL){
        if(t->dirty || t->applied || t->poisoned || w->dirty || w->applied || w->poisoned)return false;
        memset(t,0,sizeof(*t));memset(w,0,sizeof(*w));memset(q,0,sizeof(*q));memset(d,0,sizeof(*d));memset(&s->hubp_clock_target,0,sizeof(s->hubp_clock_target));
        memset(s->hubp_floor_mhz,0,sizeof(s->hubp_floor_mhz));return true;
    }
    if(op==RX6600_HUBP_PREPARE){
        if(t->dirty || t->applied || t->poisoned || w->dirty || w->applied || w->poisoned)return false;
        /* Failed recalculation invalidates the prior prepared plan. */
        memset(t,0,sizeof(*t));memset(w,0,sizeof(*w));memset(q,0,sizeof(*q));memset(d,0,sizeof(*d));
        dcn302_dml_output output;
        if(!bandwidth_plan(s,timing,prepared,&output))return false;
        s->busy=true;
        enum dcn302_hubp_error e=dcn302_hubp_prepare(&s->io,s->surface.hubp,timing,&output,t);
        if(!e && dcn302_hubbub_prepare(&s->io,s->surface.hubp,&s->reference,&output,w))e=DCN302_HUBP_READBACK;
        if(!e && dcn302_dpp_prepare(&s->io,s->surface.hubp,timing,dpp_guard,d))e=DCN302_HUBP_READBACK;
        dcn302_sync sync={.vstartup=output.vstartup,.vready=output.vready_offset,
            .vupdate_offset=output.vupdate_offset,.vupdate_width=output.vupdate_width,.display_port=false};
        if(!e && dcn302_timing_prepare(&s->io,s->route.otg,timing,&sync,timing_guard,now,q))e=DCN302_HUBP_READBACK;
        if(e){t->prepared=false;w->prepared=false;q->prepared=false;d->prepared=false;}
        if(!e){
            s->hubp_clock_target=prepared?s->dfs_transaction.after:s->dfs;
            const unsigned clocks[]={DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_SOCCLK,DCN302_SMU_PHYCLK};
            for(unsigned n=0;n<4;n++)s->hubp_floor_mhz[n]=s->smu.floor_mhz[clocks[n]];
        }
        s->busy=false;s->error=e?RX6600_BANDWIDTH:RX6600_OK;return !e;
    }
    if(!t->prepared || !w->prepared)return false;
    if(op==RX6600_HUBP_APPLY && (t->dirty || t->applied || t->poisoned || w->dirty || w->applied || w->poisoned))return false;
    s->busy=true;
    if(!resources(s)){fail(s,RX6600_RESOURCE);s->busy=false;return false;}
    enum dcn302_hubp_error e=DCN302_HUBP_OK;
    if(op==RX6600_HUBP_BLANK)e=dcn302_hubp_blank(&s->io,t->hubp,now);
    else if(op==RX6600_HUBP_RESTORE){
        dcn302_reference current;
        if(dcn302_reference_read(&s->io,s->board.reference_khz,&current) ||
           !dcn302_reference_equal(&current,&w->reference))e=DCN302_HUBP_READBACK;
        if(!e)e=dcn302_hubp_restore_disabled(&s->io,t);
        /* Do not release PSTATE/SR before the old fetch registers are back. */
        if(!e && dcn302_hubbub_restore_disabled(&s->io,w))e=DCN302_HUBP_ROLLBACK;
    }
    else{
        dcn302_dfs_snapshot current;
        if(!dcn302_smu_usable(&s->smu) || s->smu.busy || dcn302_dfs_read(&s->io,&current) ||
           memcmp(&current,&s->hubp_clock_target,sizeof(current)))e=DCN302_HUBP_READBACK;
        const unsigned clocks[]={DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_SOCCLK,DCN302_SMU_PHYCLK};
        for(unsigned n=0;n<4;n++)if(!s->smu.floor_known[clocks[n]] || s->smu.floor_mhz[clocks[n]]<s->hubp_floor_mhz[n])e=DCN302_HUBP_READBACK;
        if(!e && dcn302_hubbub_apply_disabled(&s->io,w))e=DCN302_HUBP_READBACK;
        if(!e){
            e=dcn302_hubp_apply_disabled(&s->io,t);
            if(e && !t->dirty && !t->poisoned && dcn302_hubbub_restore_disabled(&s->io,w))e=DCN302_HUBP_ROLLBACK;
        }
    }
    if(t->poisoned || w->poisoned || (e && (t->dirty || w->dirty)))fail(s,RX6600_BANDWIDTH);
    else s->error=e?RX6600_BANDWIDTH:RX6600_OK;
    s->busy=false;return !e;
}
/* Failure-time register dump for the first runs on real hardware: one dmesg capture then shows the state the VBIOS/GOP left
 * (frame buffer location, display pipes, SMU mailbox, clocks) and tells what the probe assumed wrongly. Read-only. */
static bool dump_read(rx6600_state *s,uint32_t offset,uint32_t *value){
    return s->io.read && s->io.read(s->io.context,offset,value);
}
static void dump_one(rx6600_state *s,const char *name,uint32_t offset){
    uint32_t v;
    if(dump_read(s,offset,&v))nx_logf("rx6600: reg %s @%x = %08x",name,offset,v);
    else nx_logf("rx6600: reg %s @%x unreadable",name,offset);
}
/* One line with all six mailbox registers of one candidate set: DAL (MP1 C2PMSG_74/51/52 = message/argument/response) and the PMFW driver
 * mailbox (C2PMSG_66/82/90). Set 0 is the Navi21 layout Linux hard-codes (MP1 aperture 0x16000), set 1 the Navi23 aperture 0x16200. */
static void mailbox_line(rx6600_state *s,const char *when,unsigned set){
    if(!nx_log_enabled())return;   /* diagnosis only: no register reads without a log */
    uint32_t v[6]={0};bool ok=true;
    const uint32_t add=set*0x200u;
    const uint32_t offs[6]={(0x1628au+add)*4u,(0x16273u+add)*4u,(0x16274u+add)*4u,DCN302_PMFW_MESSAGE_BYTES(set),DCN302_PMFW_ARGUMENT_BYTES(set),DCN302_PMFW_RESPONSE_BYTES(set)};
    for(unsigned i=0;i<6;i++)if(!dump_read(s,offs[i],&v[i]))ok=false;
    nx_logf("rx6600: mailbox set %u (MP1 aperture 0x%x)%s, %s: DAL msg %08x arg %08x resp %08x | PMFW msg %08x arg %08x resp %08x",set,0x16000u+add,ok?"":" (some reads failed)",when,
        v[0],v[1],v[2],v[3],v[4],v[5]);
}
/* Nonzero dwords of a register window (dword indices first .. first+count-1), six per log line: shows which parts of the power-management
 * blocks are alive without printing hundreds of zero lines. */
static void window_lines(rx6600_state *s,const char *name,uint32_t first,unsigned count){
    if(!nx_log_enabled())return;
    uint32_t off[6],val[6];unsigned n=0,total=0;
    for(unsigned i=0;i<=count;i++){
        uint32_t v=0;bool nonzero=false;
        if(i<count){nonzero=dump_read(s,(first+i)*4u,&v) && v;if(nonzero)total++;}
        if(nonzero){off[n]=first+i;val[n]=v;n++;}
        if(n==6 || (i==count && n)){
            nx_logf("rx6600: %s: %x=%08x %x=%08x %x=%08x %x=%08x %x=%08x %x=%08x",name,off[0],val[0],n>1?off[1]:0,n>1?val[1]:0,n>2?off[2]:0,n>2?val[2]:0,
                n>3?off[3]:0,n>3?val[3]:0,n>4?off[4]:0,n>4?val[4]:0,n>5?off[5]:0,n>5?val[5]:0);
            n=0;
        }
    }
    if(!total)nx_logf("rx6600: %s: all zero or unreadable",name);
}
/* Absent-SMU diagnosis: is the power-management firmware alive at all? MP1 public registers over SMN (firmware flags sit at offset 0x24/0x28 of
 * 0x3010000; bit 31 = interrupts enabled = firmware running) and the mailbox register windows of both aperture bases. */
static void smu_explore(rx6600_state *s){
    if(!nx_log_enabled())return;
    uint32_t v[16];bool ok=true;
    for(unsigned i=0;i<16;i++)if(!dcn302_smn_read(&s->io,0x03b10000u+i*4u,&v[i]))ok=false;
    nx_logf("rx6600: SMN MP1 public 03b10000..3c%s: %08x %08x %08x %08x %08x %08x %08x %08x",ok?"":" (some reads failed)",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);
    nx_logf("rx6600: SMN MP1 public 03b10020..5c: %08x %08x %08x %08x %08x %08x %08x %08x",v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]);
    window_lines(s,"MP0 C2PMSG window 16040..160af",0x16040u,0x70);
    window_lines(s,"MP1 C2PMSG window 16240..162af",0x16240u,0x70);
}
static void dump_registers(rx6600_state *s){
    nx_logf("rx6600: --- register dump after the failed probe ---");
    dump_one(s,"FB_BASE",DCN302_FB_BASE_BYTES);dump_one(s,"FB_TOP",DCN302_FB_TOP_BYTES);dump_one(s,"FB_OFFSET",DCN302_FB_OFFSET_BYTES);
    mailbox_line(s,"after the failed probe",0);mailbox_line(s,"after the failed probe",1);
    dump_one(s,"DFS_PLL",DCN302_DFS_PLL_BYTES);dump_one(s,"DFS_DENTIST",DCN302_DFS_DENTIST_BYTES);dump_one(s,"DFS_DTO_CTRL",DCN302_DFS_DTO_CTRL_BYTES);
    dump_one(s,"HUBBUB_REFCLK_CNTL",DCN302_HUBBUB_REF_BYTES);dump_one(s,"HUBBUB_GLOBAL_TIMER",DCN302_HUBBUB_TIMER_BYTES);
    for(unsigned p=0;p<5;p++){   /* everything the scan-out surface check reads, so one capture shows which condition fails */
        uint32_t v[11]={0};bool ok=true;
        static const uint8_t reg[11]={DCN302_SURFACE_R_OUT_MUX,DCN302_SURFACE_R_TOP,DCN302_SURFACE_R_BOTTOM,DCN302_SURFACE_R_OPP,DCN302_SURFACE_R_MODE,
            DCN302_SURFACE_R_TILING,DCN302_SURFACE_R_VIEW_START,DCN302_SURFACE_R_VIEW_SIZE,DCN302_SURFACE_R_CROSSBAR,DCN302_SURFACE_R_CONTROL,DCN302_SURFACE_R_BOTTOM};
        for(unsigned i=0;i<10;i++)if(!dump_read(s,dcn302_surface_register_bytes[p][reg[i]],&v[i]))ok=false;
        uint32_t src=0;if(!dump_read(s,dcn302_register_bytes[p][DCN302_R_SOURCE],&src))ok=false;
        nx_logf("rx6600: plane %u%s: OPP-mux %08x | MPCC top %08x bottom %08x opp %08x mode %08x | HUBP tiling %08x vstart %08x vsize %08x xbar %08x ctrl %08x | OTG source %08x",
            p,ok?"":" (some reads failed)",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],src);
    }
    for(unsigned p=0;p<5;p++){
        uint32_t be=0,en=0,fe=0,hpd=0;bool ok=true;
        if(!dump_read(s,dcn302_hdmi_register_bytes[p][DCN302_HDMI_R_BE],&be))ok=false;
        if(!dump_read(s,dcn302_hdmi_register_bytes[p][DCN302_HDMI_R_BE_ENABLE],&en))ok=false;
        if(!dump_read(s,dcn302_hdmi_register_bytes[p][DCN302_HDMI_R_FE],&fe))ok=false;
        if(!dump_read(s,dcn302_hpd_status_bytes[p],&hpd))ok=false;
        nx_logf("rx6600: link/stream %u%s: DIG BE %08x BE-enable %08x FE %08x | HPD %u status %08x",p,ok?"":" (some reads failed)",be,en,fe,p,hpd);
    }
    for(unsigned p=0;p<5;p++){
        uint32_t o[4]={0},h[8]={0};bool ok=true;
        const uint32_t ot[4]={dcn302_register_bytes[p][DCN302_R_CONTROL],dcn302_register_bytes[p][DCN302_R_H_TOTAL],dcn302_register_bytes[p][DCN302_R_V_TOTAL],dcn302_register_bytes[p][DCN302_R_V_CONTROL]};
        const enum dcn302_surface_register hr[8]={DCN302_SURFACE_R_CONFIG,DCN302_SURFACE_R_CLOCK,DCN302_SURFACE_R_HUBP,DCN302_SURFACE_R_ADDRESS,DCN302_SURFACE_R_ADDRESS_HIGH,DCN302_SURFACE_R_INUSE,DCN302_SURFACE_R_INUSE_HIGH,DCN302_SURFACE_R_PITCH};
        for(unsigned i=0;i<4;i++)if(!dump_read(s,ot[i],&o[i]))ok=false;
        for(unsigned i=0;i<8;i++)if(!dump_read(s,dcn302_surface_register_bytes[p][hr[i]],&h[i]))ok=false;
        nx_logf("rx6600: pipe %u%s: OTG ctl %08x htotal %08x vtotal %08x vctl %08x | HUBP cfg %08x clk %08x cntl %08x addr %08x:%08x inuse %08x:%08x pitch %08x",
                p,ok?"":" (some reads failed)",o[0],o[1],o[2],o[3],h[0],h[1],h[2],h[4],h[3],h[6],h[5],h[7]);
    }
}

/* A board-data or VRAM-table failure on real hardware cannot be reproduced here: print the raw bytes of the tables the parsers read
 * (object info, GPIO pin table, DCE info, VRAM info) so one dmesg capture shows what the video BIOS really contains. */
static void dump_hex(const char *name,const uint8_t *p,unsigned n){
    static const char hx[]="0123456789abcdef";
    for(unsigned o=0;o<n;o+=16){
        char line[16*3+1];unsigned l=0,m=n-o<16?n-o:16;
        for(unsigned i=0;i<m;i++){line[l++]=hx[p[o+i]>>4];line[l++]=hx[p[o+i]&15];line[l++]=' ';}
        line[l]=0;nx_logf("rx6600: %s +%03x: %s",name,o,line);
    }
}
static const char *table_name(unsigned index){   /* a switch, not a pointer table: the module has no runtime relocations */
    switch(index){case 22:return "object_info";case 12:return "gpio_pin_lut";case 27:return "dce_info";default:return "vram_info";}
}
static void dump_rom_tables(const atom_rom *rom){
    static const uint8_t indexes[4]={22,12,27,28};
    static const uint8_t limits[4]={224,112,64,96};
    for(unsigned i=0;i<4;i++){
        atom_table t;const char *name=table_name(indexes[i]);
        if(!atom_rom_table(rom,false,indexes[i],&t)){nx_logf("rx6600: ROM table %s (index %u): missing or invalid",name,(unsigned)indexes[i]);continue;}
        nx_logf("rx6600: ROM table %s: size %u, format %u, revision %u",name,(unsigned)t.size,(unsigned)t.format,(unsigned)t.revision);
        dump_hex(name,t.bytes,t.size<limits[i]?t.size:limits[i]);
    }
}
static enum rx6600_error probe_inner(rx6600_state *s,const nexis_gpu_services *k){
    if(!s)return RX6600_INPUT;
    memset(s,0,sizeof(*s));
    if(!k || k->abi!=2 || k->size!=NEXIS_GPU_SERVICES_LOG_BYTES || k->vendor!=0x1002 || (k->device!=0x73ff && k->device!=0x73ef) ||
       !k->width || !k->height || k->pitch<k->width || k->format>1 || k->reserved || k->reserved2 ||
       !k->framebuffer || !k->framebuffer_bytes || !k->rom || !k->rom_bytes ||
       !k->read32 || !k->write32 || !k->time_us || !k->delay_us || !k->resource)return fail(s,RX6600_INPUT);
    s->services=k;
    nx_log_attach(k);
    /* Volatile individual pointer stores avoid absolute pointer templates in
     * freestanding PIE; no runtime relocations/imports are available. */
    volatile dcn302_io *io=&s->io;io->context=s;io->read=read_reg;io->write=write_reg;io->delay_us=delay;
    volatile rx6600_state *operations=s;operations->clock_floor=clock_floor;operations->display_clocks=display_clocks;operations->bandwidth_plan=bandwidth_plan;operations->bandwidth_registers=bandwidth_registers;operations->timing_registers=timing_registers;operations->dpp_registers=dpp_registers;operations->firmware_command=firmware_command;operations->firmware_check=firmware_check;operations->sink_read=sink_read_ddc;operations->sink_write=sink_write_ddc;
    bool have_vram=k->resource(k->service_context,0,&s->vram),have_registers=k->resource(k->service_context,5,&s->registers);
    if(!have_vram || !have_registers){
        nx_logf("rx6600: PCI resource query rejected: BAR0 (VRAM) %s, BAR5 (registers) %s - see the [GPU] BAR lines above",have_vram?"ok":"REJECTED",have_registers?"ok":"REJECTED");
        return fail(s,RX6600_RESOURCE);
    }
    /* BAR5 must hold every register the driver touches; the highest one (SMU mailbox) is below 0x60000, and the kernel
     * bounds-checks each access, so any aperture of at least 512 KiB is accepted. */
    if(s->vram.reserved || s->registers.reserved || s->vram.flags!=(NEXIS_GPU_RESOURCE_MEMORY|NEXIS_GPU_RESOURCE_64BIT|NEXIS_GPU_RESOURCE_PREFETCH) ||
       !(s->registers.flags&NEXIS_GPU_RESOURCE_MEMORY) || !(s->registers.flags&NEXIS_GPU_RESOURCE_REGISTERS) ||
       (s->registers.flags&~15u) || s->registers.bytes<512*1024){
        nx_logf("rx6600: unexpected BAR layout: BAR0 %llx+%llx flags %x, BAR5 %llx+%llx flags %x",(unsigned long long)s->vram.base,(unsigned long long)s->vram.bytes,
            s->vram.flags,(unsigned long long)s->registers.base,(unsigned long long)s->registers.bytes,s->registers.flags);
        return fail(s,RX6600_RESOURCE);
    }
    /* Every failure below names its step: the first hardware run showed that "rejected (n)" alone is not enough to
     * find the cause on a card that cannot be reached from the development machine. */
    atom_rom rom;
    if(!atom_rom_open(k->rom,k->rom_bytes,k->vendor,k->device,&rom)){nx_logf("rx6600: probe stopped at: video BIOS image (%u bytes) not usable",(unsigned)k->rom_bytes);return fail(s,RX6600_ROM);}
    s->firmware_rom=rom;
    if(!rom.checksum_ok)nx_logf("rx6600: note: the video BIOS image checksum is not zero (tables are still used)");
    nx_logf("rx6600: video BIOS command list: entry 12 (SetPixelClock) 0x%x, entry 76 (transmitter) 0x%x, list at 0x%x, %u bytes from the ATOM image on",
        rom_u16(&rom,rom.commands+4+12*2),rom_u16(&rom,rom.commands+4+76*2),(unsigned)rom.commands,(unsigned)rom.size);
    unsigned detail=atom_board_open(&rom,&s->board);
    if(detail!=ATOM_BOARD_OK){nx_logf("rx6600: probe stopped at: board data of the video BIOS (code %u)",detail);dump_rom_tables(&rom);return fail(s,RX6600_BOARD);}
    detail=atom_memory_open(&rom,&s->memory);
    if(detail!=ATOM_MEMORY_OK){nx_logf("rx6600: probe stopped at: VRAM description of the video BIOS (code %u)",detail);dump_rom_tables(&rom);return fail(s,RX6600_MEMORY);}
    if(dcn302_reference_read(&s->io,s->board.reference_khz,&s->reference)){nx_logf("rx6600: probe stopped at: reference clock readback (BIOS says %u kHz)",s->board.reference_khz);return fail(s,RX6600_CLOCK);}
    detail=dcn302_route_find(&s->io,&s->board,k->width,k->height,&s->route);
    if(detail!=DCN302_ROUTE_OK){nx_logf("rx6600: probe stopped at: finding the HDMI output that UEFI lights (code %u, desktop %ux%u)",detail,k->width,k->height);return fail(s,RX6600_ROUTE);}
    detail=dcn302_surface_bind(&s->io,&s->route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&s->surface);
    if(detail!=DCN302_SURFACE_OK){nx_logf("rx6600: probe stopped at: scan-out surface does not match the desktop framebuffer (code %u)",detail);return fail(s,RX6600_SURFACE);}
    dcn302_snapshot fixed;
    if(!dcn302_otg_snapshot(&s->io,s->route.otg,&fixed)){nx_logf("rx6600: probe stopped at: OTG %u timing snapshot",s->route.otg);return fail(s,RX6600_CLOCK);}
    s->fixed_rate[0]=fixed.registers[DCN302_R_V_CONTROL];s->fixed_rate[1]=fixed.registers[DCN302_R_V_MIN];s->fixed_rate[2]=fixed.registers[DCN302_R_V_MAX];
    detail=dcn302_clock_measure(&s->io,s->route.otg,now,16,&s->clock);
    if(detail!=DCN302_OK || s->clock.pixel_khz>s->route.max_tmds_khz){nx_logf("rx6600: probe stopped at: measuring the running pixel clock (code %u, %u kHz, limit %u kHz)",detail,s->clock.pixel_khz,s->route.max_tmds_khz);return fail(s,RX6600_CLOCK);}
    if(dcn302_dfs_read(&s->io,&s->dfs)){nx_logf("rx6600: probe stopped at: display clock (DFS) readback");return fail(s,RX6600_CLOCK);}
    nx_logf("rx6600: display clocks: VCO %u kHz%s, DISPCLK %u kHz, DPPCLK %u kHz",s->dfs.vco_khz,s->dfs.vco_default?" (Linux default, PLL_REQ unusable)":"",s->dfs.disp_khz,s->dfs.dpp_khz);
    mailbox_line(s,"before any message",0);mailbox_line(s,"before any message",1);
    if(!dcn302_smu_open(&s->smu,&s->io,now)){
        nx_logf("rx6600: probe stopped at: power-management (SMU) mailbox (error %u, test reply 0x%x, version 0x%x, last response 0x%x)",
            (unsigned)s->smu.error,s->smu.raw_test,s->smu.version,s->smu.raw_response);
        return fail(s,RX6600_SMU);
    }
    if(s->smu.absent){
        /* The first real RX 6600 never answered the DAL mailbox (run 8). Linux then runs with smu_present=false and does no clock
         * management at all; this driver does the same and keeps everything the video BIOS set up. The other PMFW mailbox is
         * asked once for its version so the next log shows whether the firmware is alive at all. */
        uint32_t response[2]={0,0},version[2]={0,0};
        bool alive_pmfw[2]={false,false};
        if(nx_log_enabled()){alive_pmfw[0]=dcn302_pmfw_version(&s->io,now,0,&response[0],&version[0]);alive_pmfw[1]=dcn302_pmfw_version(&s->io,now,1,&response[1],&version[1]);}   /* diagnosis only */
        nx_logf("rx6600: power management: the SMU does not answer the DAL mailbox at either register set; PMFW driver mailbox set 0 %s (response 0x%x, version 0x%x), set 1 %s (response 0x%x, version 0x%x)",
            alive_pmfw[0]?"answers":"is silent",response[0],version[0],alive_pmfw[1]?"answers":"is silent",response[1],version[1]);
        mailbox_line(s,"after the queries",0);mailbox_line(s,"after the queries",1);
        smu_explore(s);
        /* Display clocks stay exactly as found: their voltage floor is what the BIOS state already runs at. Memory, fabric and PHY
         * clocks cannot be read back, so the bandwidth model gets the LOWEST plausible level of each (more latency, larger
         * watermarks: the safe direction). They are stand-ins, not measurements. */
        const struct {enum dcn302_smu_clock clock;uint32_t mhz;} assumed[]={
            {DCN302_SMU_SOCCLK,RX6600_ASSUMED_SOCCLK_MHZ},{DCN302_SMU_UCLK,RX6600_ASSUMED_UCLK_MHZ},{DCN302_SMU_DCEFCLK,RX6600_ASSUMED_DCEFCLK_MHZ},
            {DCN302_SMU_DISPCLK,s->dfs.disp_floor_mhz},{DCN302_SMU_DPPCLK,s->dfs.dpp_floor_mhz},{DCN302_SMU_PHYCLK,RX6600_ASSUMED_PHYCLK_MHZ}};
        for(unsigned n=0;n<6;n++)if(!dcn302_smu_assume(&s->smu,assumed[n].clock,assumed[n].mhz)){
            nx_logf("rx6600: probe stopped at: stand-in clock %u MHz for clock %u",assumed[n].mhz,(unsigned)assumed[n].clock);return fail(s,RX6600_SMU);
        }
        nx_logf("rx6600: no clock management: DISPCLK %u / DPPCLK %u MHz stay as the BIOS set them; assumed SOC %u, memory %u, DCF %u, PHY %u MHz",
            s->dfs.disp_floor_mhz,s->dfs.dpp_floor_mhz,RX6600_ASSUMED_SOCCLK_MHZ,RX6600_ASSUMED_UCLK_MHZ,RX6600_ASSUMED_DCEFCLK_MHZ,RX6600_ASSUMED_PHYCLK_MHZ);
    }
    if(!s->smu.absent)nx_logf("rx6600: power management: SMU answers on mailbox set %u (version 0x%x, test reply 0x%x)",(unsigned)s->smu.mailbox_set,s->smu.version,s->smu.raw_test);
    if(s->smu.header_mismatch || s->smu.interface_mismatch)
        nx_logf("rx6600: note: SMU header version %u (expected 1), driver interface 0x%x (expected 0x%x); continuing like Linux",
            s->smu.raw_header,s->smu.raw_interface,(unsigned)DCN302_SMU_INTERFACE);
    const enum dcn302_smu_clock clocks[]={DCN302_SMU_SOCCLK,DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_DISPCLK,DCN302_SMU_DPPCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<6 && !s->smu.absent;n++){
        dcn302_smu_limits limits;
        if(!dcn302_smu_clock_limits(&s->smu,clocks[n],&limits)){
            nx_logf("rx6600: probe stopped at: SMU clock limits for clock %u (SMU clock id %u, error %u, response 0x%x, value 0x%x)",
                n,(unsigned)clocks[n],(unsigned)s->smu.error,s->smu.raw_response,s->smu.raw_value);
            return fail(s,RX6600_SMU);
        }
    }
    enum rx6600_error error=prove(s);
    if(error){nx_logf("rx6600: probe stopped at: final consistency check (code %u)",(unsigned)error);return fail(s,error);}
    s->sampled_us=now(s);
    if(!dcn302_otg_frame_count(&s->io,s->route.otg,&s->sampled_frame)){nx_logf("rx6600: probe stopped at: OTG frame counter");return fail(s,RX6600_CLOCK);}
    if(!dcn302_ddc_init(&s->ddc,&s->io,s->route.ddc,s->board.reference_khz)){nx_logf("rx6600: probe stopped at: DDC (monitor I2C) setup");return fail(s,RX6600_ROUTE);}
    rx6600_edid_from_ddc(s);edid_report(s);
    nx_logf("rx6600: probe ok, %ux%u at %u kHz (%u.%03u Hz), HDMI path %u, OTG %u, link %u, stream %u, DDC %u, max TMDS %u kHz",
        s->route.shape.hactive,s->route.shape.vactive,s->clock.pixel_khz,s->clock.refresh_millihz/1000,s->clock.refresh_millihz%1000,
        s->route.path,s->route.otg,s->route.link,s->route.stream,s->route.ddc,s->route.max_tmds_khz);
    s->ready=true;s->error=RX6600_OK;return RX6600_OK;
}
enum rx6600_error rx6600_probe(rx6600_state *s,const nexis_gpu_services *k){
    enum rx6600_error e=probe_inner(s,k);
    if(e!=RX6600_OK && e!=RX6600_INPUT && s && s->services && nx_log_enabled())dump_registers(s);
    return e;
}
static bool alive(rx6600_state *s){
    uint64_t time=now(s);uint32_t frame;
    if(time<s->sampled_us || !dcn302_otg_frame_count(&s->io,s->route.otg,&frame))return false;
    if(time-s->sampled_us>30000000){s->sampled_us=time;s->sampled_frame=frame;return true;} /* no poll for a long while: start a fresh window instead of failing */
    uint64_t elapsed=time-s->sampled_us;
    if(elapsed<100000)return true;
    uint64_t expected=(elapsed*s->clock.refresh_millihz+500000000)/1000000000;
    uint32_t delta=(frame-s->sampled_frame)&DCN302_FRAME_COUNT_MASK;
    uint64_t tolerance=2+(expected+999)/1000;
    if(!delta || delta+tolerance<expected || (uint64_t)delta>expected+tolerance)return false;
    if(elapsed>=500000){s->sampled_us=time;s->sampled_frame=frame;}
    return true;
}
bool rx6600_read_mode(rx6600_state *s,nexis_gpu_scanout *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!s || !s->ready || s->busy || s->in_modeset)return false;
    s->busy=true;enum rx6600_error error=prove(s);
    if(!error && !alive(s))error=RX6600_CLOCK;
    if(error){fail(s,error);s->busy=false;return false;}
    out->timing=s->route.shape;out->timing.pixel_khz=s->clock.pixel_khz;
    out->framebuffer=s->surface.cpu_address;out->pitch=s->surface.pitch;out->format=s->surface.format;
    out->flags=NEXIS_GPU_SCANOUT_ACTIVE|NEXIS_GPU_SCANOUT_HDMI|NEXIS_GPU_SCANOUT_CLOCK_MEASURED|(s->audio_active?NEXIS_GPU_SCANOUT_AUDIO:0);
    s->busy=false;return true;
}
const uint8_t *rx6600_edid(const rx6600_state *s,unsigned *bytes){
    if(s->services && s->services->edid && s->services->edid_bytes>=128){*bytes=s->services->edid_bytes;return s->services->edid;}
    if(s->edid_source==2 && s->edid_bytes){*bytes=s->edid_bytes;return s->edid;}
    *bytes=0;return NULL;
}
static bool edid_block_ok(const uint8_t *b,bool first){
    unsigned sum=0;
    for(unsigned i=0;i<128;i++)sum+=b[i];
    if(sum&255)return false;
    return !first || (b[0]==0 && b[1]==255 && b[2]==255 && b[3]==255 && b[4]==255 && b[5]==255 && b[6]==255 && b[7]==0);
}
/* The firmware handed the kernel no EDID (or an unusable one): ask the monitor itself over the connector's DDC engine, like Linux does. */
void rx6600_edid_from_ddc(rx6600_state *s){
    unsigned have;
    if(rx6600_edid(s,&have))return;
    uint8_t block[256];unsigned want=128;
    for(unsigned off=0;off<want;off++){
        if(!s->sink_read(s,0x50,(uint8_t)off,&block[off])){nx_logf("rx6600: EDID over DDC: read of byte %u failed",off);return;}
    }
    if(!edid_block_ok(block,true)){nx_logf("rx6600: EDID over DDC: first block is not valid");return;}
    if(block[126]>=1){
        want=256;
        for(unsigned off=128;off<want;off++)if(!s->sink_read(s,0x50,(uint8_t)off,&block[off])){want=128;break;}
        if(want==256 && !edid_block_ok(block+128,false))want=128;
    }
    memcpy(s->edid,block,want);s->edid_bytes=want;s->edid_source=2;
    nx_logf("rx6600: EDID read over DDC: %u bytes",want);
}
static void edid_report(rx6600_state *s){
    unsigned bytes;const uint8_t *edid=rx6600_edid(s,&bytes);
    if(!edid){nx_logf("rx6600: no monitor EDID (kernel none, DDC none): HDMI audio cannot be planned");return;}
    nx_audio_caps caps;
    if(!nx_audio_caps_from_edid(edid,bytes,&caps)){nx_logf("rx6600: EDID %s (%u bytes): no HDMI audio descriptor found",s->edid_source==2?"over DDC":"from the kernel",bytes);return;}
    nx_logf("rx6600: EDID %s (%u bytes): monitor '%s', LPCM rates %02x sizes %x speakers %02x, stereo 48 kHz 16 bit %s",s->edid_source==2?"over DDC":"from the kernel",bytes,
        caps.name,caps.lpcm_rates,caps.lpcm_sizes,caps.speakers,nx_audio_caps_stereo48(&caps)?"yes":"NO");
}
bool rx6600_is_running_mode(const rx6600_state *s,const nexis_gpu_timing *t){
    if(!s || !t || !t->pixel_khz)return false;
    nexis_gpu_timing shape=*t;shape.pixel_khz=0;
    uint32_t clock=s->clock.pixel_khz;
    uint32_t difference=clock>t->pixel_khz?clock-t->pixel_khz:t->pixel_khz-clock;
    return !memcmp(&shape,&s->route.shape,sizeof(shape)) && (uint64_t)difference*1000000<=(uint64_t)t->pixel_khz*1000;
}
uint32_t rx6600_max_pixel_khz(const rx6600_state *s){
    uint32_t limit=s->route.max_tmds_khz;
    if(!s->smu.absent)return limit;
    /* Display and DPP clocks stay at the video BIOS values: one pixel per clock plus the model's down-spread and ramp margin
     * (about 2 %) leave this much; a 5 % reserve keeps the bandwidth model from refusing a mode the kernel then insists on. */
    uint32_t clock=s->dfs.disp_khz<s->dfs.dpp_khz?s->dfs.disp_khz:s->dfs.dpp_khz;
    uint32_t carried=(uint32_t)((uint64_t)clock*100/105);
    if(carried<s->clock.pixel_khz)carried=s->clock.pixel_khz;
    return carried<limit?carried:limit;
}
bool rx6600_set_mode(rx6600_state *s,const nexis_gpu_timing *t){
    nexis_gpu_scanout current;
    if(!t || !rx6600_read_mode(s,&current))return false;
    if(rx6600_is_running_mode(s,t)){
        /* The hardware already runs exactly this measured mode. Audio is only set up by the full native sequence, so an
         * unchanged mode still goes through it once when the monitor can play sound and nothing has done so yet. */
        bool audio=false;
        unsigned edid_bytes;const uint8_t *edid=rx6600_edid(s,&edid_bytes);
        if(!s->audio_active && !s->audio_tried && edid){
            nx_audio_caps caps;audio=nx_audio_caps_from_edid(edid,edid_bytes,&caps) && nx_audio_caps_stereo48(&caps);
        }
        if(!audio){s->error=RX6600_OK;return true;}
        nx_logf("rx6600: unchanged mode: running the native sequence once to set up HDMI audio");
    }
    return rx6600_modeset(s,t);
}
bool rx6600_still_valid(rx6600_state *s){return s && s->ready && !prove(s);}
bool rx6600_new_mode_guard(void *context){return timing_guard(context);}
void rx6600_resample(rx6600_state *s){
    /* The liveness check compares the frame counter with wall time since the last sample; a stop/restart in between must not look like a hang. */
    s->sampled_us=now(s);
    if(!dcn302_otg_frame_count(&s->io,s->route.otg,&s->sampled_frame))fail(s,RX6600_CLOCK);
}
void rx6600_commit_baseline(rx6600_state *s){
    /* The applied fetch/watermark/DPP/timing/clock registers now describe the *running* mode. Their rollback
     * images belong to the old mode, so drop them; the next mode switch prepares from the new hardware state. */
    memset(&s->hubp_transaction,0,sizeof(s->hubp_transaction));memset(&s->hubbub_transaction,0,sizeof(s->hubbub_transaction));
    memset(&s->timing_transaction,0,sizeof(s->timing_transaction));memset(&s->dpp_transaction,0,sizeof(s->dpp_transaction));
    memset(&s->dfs_transaction,0,sizeof(s->dfs_transaction));memset(&s->resync,0,sizeof(s->resync));
    memset(&s->hubp_clock_target,0,sizeof(s->hubp_clock_target));memset(s->hubp_floor_mhz,0,sizeof(s->hubp_floor_mhz));
}
bool rx6600_refresh_after_modeset(rx6600_state *s,const nexis_gpu_timing *measured){
    const nexis_gpu_services *k=s->services;dcn302_route route;dcn302_surface surface;
    dcn302_dfs_snapshot dfs;dcn302_reference reference;dcn302_snapshot fixed;uint32_t frame;
    /* Gather everything into locals first; the state changes only if every proof holds. */
    if(!resources(s) || dcn302_route_find(&s->io,&s->board,k->width,k->height,&route)!=DCN302_ROUTE_OK)return false;
    if(route.path!=s->route.path || route.link!=s->route.link || route.stream!=s->route.stream || route.otg!=s->route.otg ||
       route.opp!=s->route.opp || route.ddc!=s->route.ddc || route.hpd!=s->route.hpd || route.max_tmds_khz!=s->route.max_tmds_khz)return false;
    nexis_gpu_timing want=*measured;want.pixel_khz=0;
    if(memcmp(&want,&route.shape,sizeof(want)))return false;
    if(dcn302_surface_bind(&s->io,&route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&surface)!=DCN302_SURFACE_OK)return false;
    if(!dcn302_otg_snapshot(&s->io,route.otg,&fixed) || dcn302_dfs_read(&s->io,&dfs) || dcn302_reference_read(&s->io,s->board.reference_khz,&reference) ||
       !dcn302_otg_frame_count(&s->io,route.otg,&frame))return false;
    s->fixed_rate[0]=fixed.registers[DCN302_R_V_CONTROL];s->fixed_rate[1]=fixed.registers[DCN302_R_V_MIN];s->fixed_rate[2]=fixed.registers[DCN302_R_V_MAX];
    s->route=route;s->surface=surface;s->dfs=dfs;s->reference=reference;
    s->sampled_us=now(s);s->sampled_frame=frame;
    return true;
}
void rx6600_poll(rx6600_state *s){
    if(!s || !s->ready || s->busy || s->in_modeset)return;
    bool connected=false;s->busy=true;
    const enum dcn302_route_error hpd=dcn302_route_connected(&s->io,&s->board.paths[s->route.path],s->route.hpd,&connected);
    /* Monitors pulse HPD when the input is switched, after a mode change and during HDMI re-authentication: only a pin
     * that stays low for 3 s means the monitor is really gone. */
    const uint64_t t=now(s);
    if(hpd==DCN302_ROUTE_OK && connected)s->hpd_lost_us=0;
    else if(hpd==DCN302_ROUTE_OK){
        if(!s->hpd_lost_us)s->hpd_lost_us=t?t:1;
        if(t<s->hpd_lost_us || t-s->hpd_lost_us>3000000)fail(s,RX6600_ROUTE);
    }else fail(s,RX6600_ROUTE);
    if(!s->ready){}
    else if(!dcn302_smu_usable(&s->smu))fail(s,RX6600_SMU);
    else if(!fixed_rate(s) || !alive(s))fail(s,RX6600_CLOCK);
    s->busy=false;
}
void rx6600_shutdown(rx6600_state *s){if(s)memset(s,0,sizeof(*s));}
