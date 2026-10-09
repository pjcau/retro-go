/* RACE frame profiler (build with -DNGPPROF=1): exclusive time per part of a
 * Neo Geo Pocket frame, printed every 60 frames. Same shape as mame-go's
 * mamego_prof.h and retro-home's A78PROF, so the log lines read alike.
 *
 * The parts are a partition of the frame: a push attributes the time since the
 * previous event to whatever is on top of the stack, so a nested push (the Z80
 * inside a TLCS-900H memory write, say) takes its time out of the enclosing
 * part instead of double-counting it. Time outside every push lands in "other".
 *
 * -DNGPPROF_GFX=1 adds the split inside the line renderer (palette, background
 * fill, the two scroll planes, sprites). It costs five more timer reads per
 * scanline, so it is a separate gate: read the parts split from an NGPPROF
 * build and the graphics split from an NGPPROF_GFX one, never the absolute
 * frame time from the latter.
 */
#ifndef RACE_PROF_H
#define RACE_PROF_H

#ifdef NGPPROF
enum { PROF_OTHER, PROF_CPU, PROF_Z80, PROF_GFX, PROF_SND, PROF_MIX, PROF_SEND, PROF_COUNT,
       PROF_GPAL = PROF_COUNT, PROF_GBG, PROF_GSCROLL, PROF_GSPR, PROF_ALL };
void race_prof_push(int part);
void race_prof_pop(void);
void race_prof_frame(void);
#define PROF_PUSH(p) race_prof_push(p)
#define PROF_POP()   race_prof_pop()
#define PROF_FRAME() race_prof_frame()
#else
#define PROF_PUSH(p) do {} while (0)
#define PROF_POP()   do {} while (0)
#define PROF_FRAME() do {} while (0)
#endif

#if defined(NGPPROF) && defined(NGPPROF_GFX)
#define PROF_GPUSH(p) race_prof_push(p)
#define PROF_GPOP()   race_prof_pop()
#else
#define PROF_GPUSH(p) do {} while (0)
#define PROF_GPOP()   do {} while (0)
#endif

#endif /* RACE_PROF_H */
