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
/* core 1 (NEOPROF): us spent by its tasks, added by them (main.c present task,
   fm.c YM2610 task); printed with the core 0 split */
volatile int64_t mamego_core1_us[3]; /* 0 sprite render, 1 convert + display, 2 YM2610 */
volatile int mamego_render_count[3];  /* Neo Geo sprites: 0 deferred, 1 drawn on core 1, 2 drawn on core 0 as fallback */
int64_t mamego_prof_now(void) { return now_us(); }
/* host hooks (mame-go main.c): core 1 idle time and display task busy time, in us since start */
__attribute__((weak)) int64_t mamego_core1_idle_us(void) { return 0; }
__attribute__((weak)) int64_t mamego_display_busy_us(void) { return 0; }
static int64_t last, total[PROF_ALL], frame_start;
static uint64_t bytes_r[PROF_ALL], bytes_w[PROF_ALL];
static uint32_t items[PROF_ALL];

void mamego_prof_bytes(int part, unsigned read, unsigned written, unsigned n)
{
	bytes_r[part] += read;
	bytes_w[part] += written;
	items[part] += n;
}
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

volatile int64_t mamego_wait_us[2];   /* core 0 waiting for core 1: YM2610, the frame conversion */

void mamego_prof_frame(void)
{
	static const char *names[PROF_COUNT] = {"other", "68000", "z80", "ym2610", "video", "mixer", "blit", "out"};
	int i;
	account();
	if (++frames < 60)
		return;
	{
		int64_t t = now_us(), wall = frame_start ? t - frame_start : 0, busy0 = 0, busy1;
		int64_t video_split = 0, video_rest = total[PROF_VIDEO];
		for (i = PROF_COUNT; i < PROF_ALL; i++)
			video_split += total[i];
		printf("NEOPROF ms/frame:");
		for (i = 0; i < PROF_COUNT; i++)
		{
			int64_t t_i = total[i] + (i == PROF_VIDEO ? video_split : 0);
			printf(" %s %.2f", names[i], t_i / 1000.0 / frames);
			if (i != PROF_BLIT)
				busy0 += t_i;
			total[i] = 0;
		}
		/* V0: the video split and what each part moves through PSRAM (KB per frame) */
		printf("\nNEOPROF video ms/frame: palette %.2f clear %.2f sprites %.2f fix %.2f rest %.2f"
			" | KB/frame: clear w %.1f sprites r %.1f w<=%.1f strips %.0f fix r %.1f w<=%.1f tiles %.0f\n",
			total[PROF_VPAL] / 1000.0 / frames, total[PROF_VCLEAR] / 1000.0 / frames,
			total[PROF_VSPR] / 1000.0 / frames, total[PROF_VFIX] / 1000.0 / frames,
			video_rest / 1000.0 / frames,
			bytes_w[PROF_VCLEAR] / 1024.0 / frames,
			bytes_r[PROF_VSPR] / 1024.0 / frames, bytes_w[PROF_VSPR] / 1024.0 / frames, (double)items[PROF_VSPR] / frames,
			bytes_r[PROF_VFIX] / 1024.0 / frames, bytes_w[PROF_VFIX] / 1024.0 / frames, (double)items[PROF_VFIX] / frames);
		for (i = PROF_COUNT; i < PROF_ALL; i++)
			total[i] = bytes_r[i] = bytes_w[i] = items[i] = 0;
		busy1 = mamego_core1_us[0] + mamego_core1_us[1] + mamego_core1_us[2];
		printf("\nNEOPROF core1 ms/frame: sprites %.2f convert+display %.2f ym2610 %.2f",
			mamego_core1_us[0] / 1000.0 / frames, mamego_core1_us[1] / 1000.0 / frames, mamego_core1_us[2] / 1000.0 / frames);
		{
			static int64_t idle_prev, disp_prev;
			int64_t idle = mamego_core1_idle_us(), disp = mamego_display_busy_us();
			printf(" display %.2f", (disp - disp_prev) / 1000.0 / frames);
			if (wall)
				printf(" | busy core0 %d%% (without blit/wait) core1 %d%% (idle-measured)",
					(int)(busy0 * 100 / wall), (int)(100 - (idle - idle_prev) * 100 / wall));
			idle_prev = idle;
			disp_prev = disp;
		}
		printf(" | sprites deferred %d on core1 %d fallback %d\n", mamego_render_count[0], mamego_render_count[1], mamego_render_count[2]);
		printf("NEOPROF core0 waits ms/frame: ym2610 %.2f present %.2f\n", mamego_wait_us[0] / 1000.0 / frames, mamego_wait_us[1] / 1000.0 / frames);
		mamego_wait_us[0] = mamego_wait_us[1] = 0;
		mamego_render_count[0] = mamego_render_count[1] = mamego_render_count[2] = 0;
		mamego_core1_us[0] = mamego_core1_us[1] = mamego_core1_us[2] = 0;
		frame_start = t;
	}
	frames = 0;
}
#endif
