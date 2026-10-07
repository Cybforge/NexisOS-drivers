#ifndef NEXIS_DML_LOGGER_H
#define NEXIS_DML_LOGGER_H
#include "nexis_dml_port.h"
/* DML's optional verbose prints are omitted from the kernel module. Assertions
 * remain active and abort the scoped computation rather than panic the OS. */
void nexis_dml_unused_log(const char *,...);
/* Keep upstream debug-only variables type-checked/used while constant-false
 * elimination removes the function and all argument evaluation/imports. */
#define dml_print(...) do{if(0)nexis_dml_unused_log(__VA_ARGS__);}while(0)
#define DTRACE(...) dml_print(__VA_ARGS__)
#endif
