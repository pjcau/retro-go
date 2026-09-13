/* Renderer counters for the esp32-emu-turbo Phase 4 work (step 4.0).
 *
 * Compiled in only with SNES_PROF=1 (see retro-core/CMakeLists.txt). Unlike
 * RG_ENABLE_PROFILING this adds no -finstrument-functions, so the numbers
 * are those of the production code plus a few counters and timer reads.
 * All fields are per-second accumulators, reset by main_snes.c after it
 * prints them.
 */
#ifndef _SNES_PROF_H_
#define _SNES_PROF_H_

#include <stdint.h>

#if SNES_PROF

typedef struct
{
   uint32_t strips;       /* S9xUpdateScreen calls (strips drawn) */
   uint32_t strip_lines;  /* scanlines drawn */
   uint32_t sub_passes;   /* RenderScreen(sub) calls (colour math on) */
   uint32_t tiles;        /* tile draws entered (TILE_PREAMBLE_CODE) */
   uint32_t tiles_blank;  /* of which rejected as BLANK_TILE */
   uint32_t tile_conv;    /* ConvertTile calls (tile cache misses) */
   uint32_t obj_setup;    /* S9xSetupOBJ calls */
   uint32_t mode_hist[8]; /* strips per BG mode */
   int64_t t_update;      /* whole S9xUpdateScreen */
   int64_t t_clear;       /* z-buffer / sub-screen clears */
   int64_t t_sub;         /* RenderScreen(sub) */
   int64_t t_main;        /* RenderScreen(main) */
   int64_t t_combine;     /* colour-math combine / backdrop fill after main */
   int64_t t_obj;         /* DrawOBJS (main + sub) */
   int64_t t_bg[4];       /* DrawBackground per BG (main + sub) */
   int64_t t_objsetup;    /* S9xSetupOBJ */
} snes_prof_t;

extern snes_prof_t snes_prof;

#define SNES_PROF_INC(field)      (snes_prof.field++)
#define SNES_PROF_ADDN(field, n)  (snes_prof.field += (n))
#define SNES_PROF_T0(var)         int64_t var = rg_system_timer()
#define SNES_PROF_ACC(field, var) (snes_prof.field += rg_system_timer() - (var))

#else

#define SNES_PROF_INC(field)      ((void)0)
#define SNES_PROF_ADDN(field, n)  ((void)0)
#define SNES_PROF_T0(var)         ((void)0)
#define SNES_PROF_ACC(field, var) ((void)0)

#endif

#endif
