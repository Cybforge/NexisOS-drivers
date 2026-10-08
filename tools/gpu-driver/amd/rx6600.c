#include "rx6600.h"
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
    if(!s->smu.ready || s->smu.poisoned)return RX6600_SMU;
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
    i->vco_khz_q32=(((uint64_t)(dfs->pll&DCN302_DFS_INTEGER_MASK)<<32)|(dfs->pll&DCN302_DFS_FRACTION_MASK))*100000u;
    unsigned line;enum nexis_dml_scope_result scope=dcn302_dml_calculate(j,&line);
    if(scope!=NEXIS_DML_SCOPE_OK || j->error!=DCN302_DML_OK){s->error=RX6600_BANDWIDTH;goto done;}
    *out=j->output;ok=true;s->error=RX6600_OK;
done:
    s->busy=false;return ok;
}
static bool dpp_guard(void *context){
    rx6600_state *s=context;dcn302_dfs_snapshot current;
    if(!s || !s->services || !resources(s) || !s->smu.ready || s->smu.busy || s->smu.poisoned ||
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
static bool firmware_guard(rx6600_state *s){
    if(s->firmware_poisoned || !timing_guard(s))return false;
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
    if(!firmware_offset(s,space,index,&offset))return false;
    if(!firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    if(!s->io.read(s,offset,&value))return firmware_fault(s,ATOM_VM_IO);
    if(!firmware_guard(s))return firmware_fault(s,ATOM_VM_IO);
    *out=value;return true;
}
static bool firmware_write(void *context,enum atom_vm_space space,uint32_t index,uint32_t value){
    rx6600_state *s=context;uint32_t offset;
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
static enum atom_vm_error NEXIS_GPU_CALL firmware_command(void *context,enum atom_display_command command,uint32_t khz,unsigned action){
    rx6600_state *s=context;
    if(!s || !s->services || !s->ready || s->busy || s->firmware_poisoned || !s->dpp_transaction.applied ||
       s->route.path>=s->board.count || (khz!=s->timing_transaction.timing.pixel_khz && khz!=s->clock.pixel_khz))return ATOM_VM_INPUT;
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
    if(!s->firmware_vm.ready){
        atom_vm_io io;volatile atom_vm_io *operations=&io;
        operations->context=s;operations->read=firmware_read;operations->write=firmware_write;operations->delay_us=firmware_delay;operations->time_us=now;
        if(!atom_vm_init(&s->firmware_vm,&s->firmware_rom,&io,s->firmware_scratch,sizeof(s->firmware_scratch)))s->firmware_error=s->firmware_vm.error;
    }
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
        if(!s->smu.ready || s->smu.busy || s->smu.poisoned || dcn302_dfs_read(&s->io,&current) ||
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
enum rx6600_error rx6600_probe(rx6600_state *s,const nexis_gpu_services *k){
    if(!s)return RX6600_INPUT;
    memset(s,0,sizeof(*s));
    if(!k || k->abi!=2 || k->size!=NEXIS_GPU_SERVICES_RESOURCE_BYTES || k->vendor!=0x1002 || k->device!=0x73ff ||
       !k->width || !k->height || k->pitch<k->width || k->format>1 || k->reserved || k->reserved2 ||
       !k->framebuffer || !k->framebuffer_bytes || !k->rom || !k->rom_bytes ||
       !k->read32 || !k->write32 || !k->time_us || !k->delay_us || !k->resource)return fail(s,RX6600_INPUT);
    s->services=k;
    /* Volatile individual pointer stores avoid absolute pointer templates in
     * freestanding PIE; no runtime relocations/imports are available. */
    volatile dcn302_io *io=&s->io;io->context=s;io->read=read_reg;io->write=write_reg;io->delay_us=delay;
    volatile rx6600_state *operations=s;operations->clock_floor=clock_floor;operations->display_clocks=display_clocks;operations->bandwidth_plan=bandwidth_plan;operations->bandwidth_registers=bandwidth_registers;operations->timing_registers=timing_registers;operations->dpp_registers=dpp_registers;operations->firmware_command=firmware_command;
    if(!k->resource(k->service_context,0,&s->vram) || !k->resource(k->service_context,5,&s->registers) ||
       s->vram.reserved || s->registers.reserved || s->vram.flags!=(NEXIS_GPU_RESOURCE_MEMORY|NEXIS_GPU_RESOURCE_64BIT|NEXIS_GPU_RESOURCE_PREFETCH) ||
       !(s->registers.flags&NEXIS_GPU_RESOURCE_MEMORY) || !(s->registers.flags&NEXIS_GPU_RESOURCE_REGISTERS) ||
       (s->registers.flags&~15u) || s->registers.bytes<1024*1024)return fail(s,RX6600_RESOURCE);
    atom_rom rom;
    if(!atom_rom_open(k->rom,k->rom_bytes,k->vendor,k->device,&rom))return fail(s,RX6600_ROM);
    s->firmware_rom=rom;
    if(atom_board_open(&rom,&s->board)!=ATOM_BOARD_OK)return fail(s,RX6600_BOARD);
    if(atom_memory_open(&rom,&s->memory)!=ATOM_MEMORY_OK)return fail(s,RX6600_MEMORY);
    if(dcn302_reference_read(&s->io,s->board.reference_khz,&s->reference))return fail(s,RX6600_CLOCK);
    if(dcn302_route_find(&s->io,&s->board,k->width,k->height,&s->route)!=DCN302_ROUTE_OK)return fail(s,RX6600_ROUTE);
    if(dcn302_surface_bind(&s->io,&s->route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&s->surface)!=DCN302_SURFACE_OK)return fail(s,RX6600_SURFACE);
    dcn302_snapshot fixed;
    if(!dcn302_otg_snapshot(&s->io,s->route.otg,&fixed))return fail(s,RX6600_CLOCK);
    s->fixed_rate[0]=fixed.registers[DCN302_R_V_CONTROL];s->fixed_rate[1]=fixed.registers[DCN302_R_V_MIN];s->fixed_rate[2]=fixed.registers[DCN302_R_V_MAX];
    if(dcn302_clock_measure(&s->io,s->route.otg,now,16,&s->clock)!=DCN302_OK ||
       s->clock.pixel_khz>s->route.max_tmds_khz)return fail(s,RX6600_CLOCK);
    if(dcn302_dfs_read(&s->io,&s->dfs))return fail(s,RX6600_CLOCK);
    if(!dcn302_smu_open(&s->smu,&s->io,now))return fail(s,RX6600_SMU);
    const enum dcn302_smu_clock clocks[]={DCN302_SMU_SOCCLK,DCN302_SMU_UCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_DISPCLK,DCN302_SMU_DPPCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<6;n++){
        dcn302_smu_limits limits;
        if(!dcn302_smu_clock_limits(&s->smu,clocks[n],&limits))return fail(s,RX6600_SMU);
    }
    enum rx6600_error error=prove(s);if(error)return fail(s,error);
    s->sampled_us=now(s);
    if(!dcn302_otg_frame_count(&s->io,s->route.otg,&s->sampled_frame))return fail(s,RX6600_CLOCK);
    s->ready=true;s->error=RX6600_OK;return RX6600_OK;
}
static bool alive(rx6600_state *s){
    uint64_t time=now(s);uint32_t frame;
    if(time<s->sampled_us || time-s->sampled_us>30000000 || !dcn302_otg_frame_count(&s->io,s->route.otg,&frame))return false;
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
    if(!s || !s->ready || s->busy)return false;
    s->busy=true;enum rx6600_error error=prove(s);
    if(!error && !alive(s))error=RX6600_CLOCK;
    if(error){fail(s,error);s->busy=false;return false;}
    out->timing=s->route.shape;out->timing.pixel_khz=s->clock.pixel_khz;
    out->framebuffer=s->surface.cpu_address;out->pitch=s->surface.pitch;out->format=s->surface.format;
    out->flags=NEXIS_GPU_SCANOUT_ACTIVE|NEXIS_GPU_SCANOUT_HDMI|NEXIS_GPU_SCANOUT_CLOCK_MEASURED;
    s->busy=false;return true;
}
bool rx6600_set_mode(rx6600_state *s,const nexis_gpu_timing *t){
    nexis_gpu_scanout current;
    if(!t || !rx6600_read_mode(s,&current))return false;
    nexis_gpu_timing shape=*t;shape.pixel_khz=0;
    uint32_t clock=current.timing.pixel_khz;
    uint32_t difference=clock>t->pixel_khz?clock-t->pixel_khz:t->pixel_khz-clock;
    if(!t->pixel_khz || memcmp(&shape,&s->route.shape,sizeof(shape)) || (uint64_t)difference*1000000>(uint64_t)t->pixel_khz*1000){
        s->error=RX6600_MODESET_PENDING;return false;
    }
    /* Genuine no-op for the already running, measured mode only. */
    s->error=RX6600_OK;return true;
}
void rx6600_poll(rx6600_state *s){
    if(!s || !s->ready || s->busy)return;
    bool connected=false;s->busy=true;
    if(dcn302_route_connected(&s->io,&s->board.paths[s->route.path],s->route.hpd,&connected)!=DCN302_ROUTE_OK || !connected)
        fail(s,RX6600_ROUTE);
    else if(!s->smu.ready || s->smu.poisoned)fail(s,RX6600_SMU);
    else if(!fixed_rate(s) || !alive(s))fail(s,RX6600_CLOCK);
    s->busy=false;
}
void rx6600_shutdown(rx6600_state *s){if(s)memset(s,0,sizeof(*s));}
