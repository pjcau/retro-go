/* mame-go frame profiler (build with -DNEOPROF): exclusive time per part of
 * the emulation, printed as ms per frame every 60 frames. A part entered
 * inside another (the YM2610 synthesised while the Z80 writes a register)
 * is not counted twice: the outer part pauses while the inner one runs. */
#ifndef MAMEGO_PROF_H
#define MAMEGO_PROF_H
#ifdef NEOPROF
enum { PROF_OTHER, PROF_CPU0, PROF_CPU1, PROF_YM, PROF_VIDEO, PROF_MIXER, PROF_BLIT, PROF_OUT, PROF_COUNT,
       /* V0 of the Arcade 60 fps plan: the parts of the Neo Geo video time, nested
          inside PROF_VIDEO (vidhrdw/neogeo.c). Printed on their own line; the main
          line's "video" still holds their sum, so mbsum.py reads as before. */
       PROF_VPAL = PROF_COUNT, PROF_VCLEAR, PROF_VSPR, PROF_VFIX, PROF_VCOPY, PROF_ALL };
       /* PROF_VCOPY: the band -> frame bitmap copy of NEOBAND builds (V1 only; V2 removes it) */
void mamego_prof_push(int part);
void mamego_prof_pop(void);
void mamego_prof_frame(void);
/* bytes a part moves through the frame bitmap / tile data (PSRAM on the board):
   exact for reads and for the clear, an upper bound for the sprite and fix
   writes (transparent pixels are skipped by the plotters, not counted here) */
void mamego_prof_bytes(int part, unsigned read, unsigned written, unsigned items);
#define PROF_PUSH(p) mamego_prof_push(p)
#define PROF_POP()   mamego_prof_pop()
#define PROF_FRAME() mamego_prof_frame()
#define PROF_BYTES(p, r, w, n) mamego_prof_bytes(p, r, w, n)
#else
#define PROF_PUSH(p) do {} while (0)
#define PROF_POP()   do {} while (0)
#define PROF_FRAME() do {} while (0)
#define PROF_BYTES(p, r, w, n) do {} while (0)
#endif
#endif
