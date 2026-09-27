/* See src/mamego_prof.h */
#ifdef NEOPROF
#include <stdio.h>
#include <stdint.h>
#include "mamego_prof.h"
#ifdef ESP_PLATFORM
#include <esp_timer.h>
static int64_t now_us(void) { return esp_timer_get_time(); }
#else
#include <time.h>
static int64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }
#endif

static int stack[8], depth;
static int64_t last, total[PROF_COUNT], frame_start;
static int frames;

static void account(void)
{
	int64_t t = now_us();
	if (last)
		total[depth ? stack[depth - 1] : PROF_OTHER] += t - last;
	last = t;
}

void mamego_prof_push(int part)
{
	account();
	if (depth < 8)
		stack[depth++] = part;
}

void mamego_prof_pop(void)
{
	account();
	if (depth)
		depth--;
}

void mamego_prof_frame(void)
{
	static const char *names[PROF_COUNT] = {"other", "68000", "z80", "ym2610", "video", "mixer", "blit", "out"};
	int i;
	account();
	if (++frames < 60)
		return;
	printf("NEOPROF ms/frame:");
	for (i = 0; i < PROF_COUNT; i++)
	{
		printf(" %s %.2f", names[i], total[i] / 1000.0 / frames);
		total[i] = 0;
	}
	printf("\n");
	frames = 0;
}
#endif
