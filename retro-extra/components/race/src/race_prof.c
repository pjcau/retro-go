#ifdef NGPPROF
#include <stdio.h>
#include <stdint.h>
#include "race_prof.h"
#ifdef ESP_PLATFORM
#include <esp_timer.h>
static int64_t now_us(void) { return esp_timer_get_time(); }
#else
#include <time.h>
static int64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }
#endif

static int stack[8], depth;
static int64_t last, total[PROF_ALL], frame_start;
static int frames;

static void account(void)
{
	int64_t t = now_us();
	if (last)
		total[depth ? stack[depth - 1] : PROF_OTHER] += t - last;
	last = t;
}

void race_prof_push(int part)
{
	account();
	if (depth < 8)
		stack[depth++] = part;
}

void race_prof_pop(void)
{
	account();
	if (depth)
		depth--;
}

void race_prof_frame(void)
{
	static const char *names[PROF_COUNT] = {"other", "tlcs", "z80", "gfx", "snd", "mix", "send"};
	int i;
	int64_t t, wall, busy = 0, gfx_split = 0, gfx_rest;

	account();
	if (++frames < 60)
		return;

	t = now_us();
	wall = frame_start ? t - frame_start : 0;
	gfx_rest = total[PROF_GFX];
	/* The graphics sub-parts are time taken out of "gfx", so add them back
	 * before printing it: the parts line then sums to the frame either way. */
	for (i = PROF_COUNT; i < PROF_ALL; i++)
		gfx_split += total[i];

	printf("NGPPROF ms/frame:");
	for (i = 0; i < PROF_COUNT; i++)
	{
		int64_t t_i = total[i] + (i == PROF_GFX ? gfx_split : 0);
		printf(" %s %.2f", names[i], t_i / 1000.0 / frames);
		busy += t_i;
		total[i] = 0;
	}
	if (wall)
		printf(" | frame %.2f busy %d%%", wall / 1000.0 / frames, (int)(busy * 100 / wall));
	printf("\n");
#ifdef NGPPROF_GFX
	printf("NGPPROF gfx ms/frame: palette %.2f background %.2f scroll %.2f sprites %.2f rest %.2f\n",
		total[PROF_GPAL] / 1000.0 / frames, total[PROF_GBG] / 1000.0 / frames,
		total[PROF_GSCROLL] / 1000.0 / frames, total[PROF_GSPR] / 1000.0 / frames,
		gfx_rest / 1000.0 / frames);
#endif
	for (i = PROF_COUNT; i < PROF_ALL; i++)
		total[i] = 0;
	frame_start = t;
	frames = 0;
}
#endif
