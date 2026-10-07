#ifndef NEXIS_DISPLAY_H
#define NEXIS_DISPLAY_H
#include "edid.h"
#include "../../include/bootinfo.h"
void display_monitor_init(const nexis_boot_info_t *);
const edid_monitor *display_monitor_get(void);
void display_set_native_mode(const edid_timing *);
void display_clear_native_mode(void);
uint32_t display_active_refresh_millihz(void);
#endif
