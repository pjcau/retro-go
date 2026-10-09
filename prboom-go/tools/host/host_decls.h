/* Forced include for the host build of prboom-go's engine: declares the few
 * retro-go helpers the fork calls unguarded. */
#ifndef HOST_DECLS_H
#define HOST_DECLS_H
#include <stddef.h>
#include <stdio.h>
void rg_gui_draw_loading(int percent);
size_t rg_storage_fread_raw(void *buffer, size_t length, FILE *fp);
#endif
