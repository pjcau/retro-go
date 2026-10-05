/* ----------------------------------------------------------------------
 * mame-go: the System 16 sound board on core 1, as the CPS1's
 * (drivers/cps1.c). Included by drivers/system16.c after its sound maps.
 *
 * The 68000 only writes the command latch (sound_command_w: latch + IRQ, or
 * latch + NMI) and never reads the board back, so the board runs one frame
 * behind on core 1: the private Z80 (cpu/z80/z80snd.c) on this board's map,
 * the YM2151's timers and the uPD7759's sample clock counted in Z80 cycles,
 * and MAME's streams updated from core 1 with the Z80's position as their
 * clock (sndintrf.c mamego_snd_pos). Core 0 only mixes them at the frame
 * end, after waiting for the job (mamego_sndboard_pre).
 *
 * Only the two standard boards move: Z80 + YM2151 (sound_readmem /
 * sound_writeport) and Z80 + YM2151 + uPD7759 in slave mode
 * (sound_readmem_7759 / sound_writeport_7759). The 7751 boards stay.
 * PC: S16SND1=0 keeps the board on core 0; board: the file sys16_nosnd1.
 * ---------------------------------------------------------------------- */
#include "sound/fm.h"
extern int z80snd_execute(int cycles);
extern void z80snd_reset(void *param);
extern void z80snd_set_context(void *src);
extern unsigned z80snd_get_context(void *dst);
extern void z80snd_set_irq_line(int irqline, int state);
extern void z80snd_set_nmi_line(int state);
extern void z80snd_set_irq_callback(int (*callback)(int irqline));
extern int z80snd_ICount;
extern unsigned z80snd_idle_enable;
extern unsigned z80_get_context(void *dst);
extern unsigned (*sndz80_rm)(unsigned); extern void (*sndz80_wm)(unsigned, unsigned);
extern unsigned (*sndz80_in)(unsigned); extern void (*sndz80_out)(unsigned, unsigned);
extern volatile int mamego_snd_pos;
extern void (*mamego_sndboard_pre)(void);
extern void (*mamego_sndboard_post)(void);
extern void (*ym2151_timer_hook)(int c, int count, timer_tm step);
extern void YM2151_timers_to_host(void);
extern void (*upd7759_timer_hook)(int num, int on, int hz);
extern void UPD7759_timers_to_host(void);
extern void UPD7759_dac_tick(int num);
extern void streams_fill_to_end(void);

#define S16SND_CLOCK 4096000
#define S16SND_EVQ   256
enum { S16EV_LATCH, S16EV_IRQ, S16EV_NMI };
struct s16snd_event { int32_t at; uint8_t kind, value; };
static struct s16snd_event s16snd_evq[2][S16SND_EVQ];
static int s16snd_evn[2], s16snd_evfill, s16snd_job_q;
static int32_t s16snd_cpf, s16snd_now, s16snd_seg_start, s16snd_seg_len;
static int s16snd_timer_on[2];
static int32_t s16snd_timer_at[2];
static int s16snd_dac_on;
static int32_t s16snd_dac_at, s16snd_dac_period;
static float s16snd_t0, s16snd_frame_len;
static unsigned char *s16snd_mem;
static int s16snd_latch, s16snd_7759;

static int32_t s16snd_time(void)
{
	return s16snd_seg_start + (s16snd_seg_len - z80snd_ICount);
}

static void s16snd_sync_streams(void)   /* the streams' clock = the Z80's */
{
	int32_t t = s16snd_time();
	if (t < 0) t = 0;
	if (t > s16snd_cpf) t = s16snd_cpf;
	mamego_snd_pos = (int)(((int64_t)t << 16) / s16snd_cpf);
}

/* sound_readmem(_7759) */
static unsigned s16snd_rm(unsigned a)
{
	a &= 0xffff;
	if (a < 0x8000 || a >= 0xf800) return s16snd_mem[a];
	if (a == 0xe800) return s16snd_latch;
	if (s16snd_7759 && a < 0xe000) return UPD7759_0_data_r(a - 0x8000);
	return s16snd_mem[a]; /* unmapped: MAME's mrh_error reads the region */
}

/* sound_writemem */
static void s16snd_wm(unsigned a, unsigned v)
{
	a &= 0xffff;
	if (a >= 0xf800) s16snd_mem[a] = v;
}

/* sound_readport */
static unsigned s16snd_in(unsigned p)
{
	switch (p & 0xff)
	{
		case 0x01: s16snd_sync_streams(); return YM2151_status_port_0_r(0);
		case 0xc0: return s16snd_latch;
	}
	return 0;
}

/* sound_writeport(_7759) */
static void s16snd_out(unsigned p, unsigned v)
{
	switch (p & 0xff)
	{
		case 0x00: s16snd_sync_streams(); YM2151_register_port_0_w(0, v); break;
		case 0x01: s16snd_sync_streams(); YM2151_data_port_0_w(0, v); break;
		case 0x40: if (s16snd_7759) { s16snd_sync_streams(); UPD7759_process_message_w(0, v); } break;
		case 0x80: if (s16snd_7759) { s16snd_sync_streams(); UPD7759_0_start_w(0, v); } break;
	}
}

/* cpu_cause_interrupt(1, 0) is a held IRQ with vector 0: cleared when taken */
static int s16snd_irq_ack(int irqline)
{
	z80snd_set_irq_line(0, CLEAR_LINE);
	return 0;
}

static void s16snd_timer(int c, int count, timer_tm step)
{
	if (!count)
		s16snd_timer_on[c] = 0;
	else if (!s16snd_timer_on[c])
	{
		s16snd_timer_on[c] = 1;
		s16snd_timer_at[c] = s16snd_time() + (int32_t)((int64_t)count * step * (S16SND_CLOCK / 1000) / (TIME_ONE_SEC / 1000));
	}
}

/* the uPD7759's sample clock (MAME: timer_pulse at base_rate) */
static void s16snd_dac_timer(int num, int on, int hz)
{
	s16snd_dac_on = on;
	if (on)
	{
		s16snd_dac_period = S16SND_CLOCK / hz;
		s16snd_dac_at = s16snd_time() + s16snd_dac_period;
	}
}

/* core 1: one frame of the board */
static void s16snd_job(void)
{
	int q = s16snd_job_q, ev = 0, n = s16snd_evn[q], c;
	while (s16snd_now < s16snd_cpf)
	{
		int32_t target = s16snd_cpf;
		if (ev < n && s16snd_evq[q][ev].at < target)
			target = s16snd_evq[q][ev].at;
		for (c = 0; c < 2; c++)
			if (s16snd_timer_on[c] && s16snd_timer_at[c] < target)
				target = s16snd_timer_at[c];
		if (s16snd_dac_on && s16snd_dac_at < target)
			target = s16snd_dac_at;
		if (target > s16snd_now + s16snd_cpf / 16)
			target = s16snd_now + s16snd_cpf / 16;
		if (target > s16snd_now)
		{
			s16snd_seg_start = s16snd_now;
			s16snd_seg_len = target - s16snd_now;
			z80snd_ICount = s16snd_seg_len;
			s16snd_now += z80snd_execute(s16snd_seg_len);
			s16snd_seg_start = s16snd_now;
			s16snd_seg_len = 0;
			z80snd_ICount = 0;
		}
		while (ev < n && s16snd_evq[q][ev].at <= s16snd_now)
		{
			switch (s16snd_evq[q][ev].kind)
			{
				case S16EV_LATCH: s16snd_latch = s16snd_evq[q][ev].value; break;
				case S16EV_IRQ: z80snd_set_irq_line(0, ASSERT_LINE); break;
				case S16EV_NMI: z80snd_set_nmi_line(ASSERT_LINE); z80snd_set_nmi_line(CLEAR_LINE); break;
			}
			ev++;
		}
		for (c = 0; c < 2; c++)
			if (s16snd_timer_on[c] && s16snd_timer_at[c] <= s16snd_now)
			{
				s16snd_timer_on[c] = 0;
				s16snd_sync_streams();
				YM2151TimerOver(0, c);
			}
		while (s16snd_dac_on && s16snd_dac_at <= s16snd_now)
		{
			s16snd_dac_at += s16snd_dac_period;
			s16snd_sync_streams();
			UPD7759_dac_tick(0); /* every other tick: the NMI that feeds it a byte */
		}
	}
	mamego_snd_pos = 65536;
	streams_fill_to_end();
	mamego_snd_pos = -1;
	s16snd_now -= s16snd_cpf;
	for (c = 0; c < 2; c++)
		s16snd_timer_at[c] -= s16snd_cpf;
	s16snd_dac_at -= s16snd_cpf;
	s16snd_evn[q] = 0;
}

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
static TaskHandle_t s16snd_task;
static SemaphoreHandle_t s16snd_done;
static volatile int s16snd_busy;
static void s16snd_task_main(void *arg)
{
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		s16snd_job();
		__sync_synchronize();
		s16snd_busy = 0;
		xSemaphoreGive(s16snd_done);
	}
}
/* the flag decides, not the token (as drivers/cps1.c) */
static void s16snd_wait(void) { while (s16snd_busy) xSemaphoreTake(s16snd_done, portMAX_DELAY); }
static int s16snd_start_task(void)
{
	s16snd_done = xSemaphoreCreateBinary();
	return s16snd_done && xTaskCreatePinnedToCore(s16snd_task_main, "s16_sound", 12288, NULL, 5, &s16snd_task, 1) == pdPASS;
}
static void s16snd_start(void) { s16snd_busy = 1; __sync_synchronize(); xTaskNotifyGive(s16snd_task); }
#else
static void s16snd_wait(void) {}
static int s16snd_start_task(void) { return 1; }
static void s16snd_start(void) { s16snd_job(); }
#endif

/* 68000, core 0: a write to the board, timed within this frame */
static void s16snd_event(int kind, int value)
{
	int f = s16snd_evfill;
	int32_t at = (int32_t)((timer_get_time() - s16snd_t0) / s16snd_frame_len * s16snd_cpf);
	if (at < 0) at = 0;
	if (at >= s16snd_cpf) at = s16snd_cpf - 1;
	if (s16snd_evn[f] && at < s16snd_evq[f][s16snd_evn[f] - 1].at)
		at = s16snd_evq[f][s16snd_evn[f] - 1].at;
	if (s16snd_evn[f] < S16SND_EVQ)
	{
		s16snd_evq[f][s16snd_evn[f]].at = at;
		s16snd_evq[f][s16snd_evn[f]].kind = kind;
		s16snd_evq[f][s16snd_evn[f]].value = value;
		s16snd_evn[f]++;
	}
}

/* core 0, sound_update: before the mix, after it */
static void s16snd_pre(void) { s16snd_wait(); }
static void s16snd_post(void)
{
	s16snd_job_q = s16snd_evfill;
	s16snd_evfill ^= 1;
	s16snd_evn[s16snd_evfill] = 0;
	s16snd_t0 = timer_get_time();
	s16snd_start();
}

/* a save state carries MAME's Z80, not the private one: hand it over both ways */
extern void (*mamego_state_hook)(int mode);
extern void mamego_cpu_context(int cpunum, void *ctx, int set);
static void s16snd_state_hook(int mode) /* 1 before a save, 2 after a load */
{
	unsigned char ctx[1024];
	s16snd_wait();
	if (mode == 1)
	{
		z80snd_get_context(ctx);
		mamego_cpu_context(1, ctx, 1);
	}
	else if (mode == 2)
	{
		mamego_cpu_context(1, ctx, 0);
		z80snd_set_context(ctx);
		z80snd_set_irq_callback(s16snd_irq_ack);
		s16snd_evn[0] = s16snd_evn[1] = 0;
		s16snd_now = 0;
		s16snd_timer_on[0] = s16snd_timer_on[1] = 0;
		s16snd_latch = soundlatch_r(0);
		s16snd_t0 = timer_get_time();
	}
}

static void s16snd_enable(void)
{
	unsigned char ctx[1024];
	extern uint8_t *z80_shared_SZHVC_add, *z80_shared_SZHVC_sub;
	const struct MachineCPU *snd = &Machine->drv->cpu[1];
#ifndef ESP_PLATFORM
	if (getenv("S16SND1") && !strcmp(getenv("S16SND1"), "0"))
		return;
#else
	{
		FILE *f = fopen("/sd/retro-go/mame/sys16_nosnd1", "r");
		if (f) { fclose(f); printf("sys16: sound board stays on core 0 (sys16_nosnd1)\n"); return; }
	}
#endif
	if (Machine->drv->sound[0].sound_type != SOUND_YM2151 || (snd->cpu_type & ~CPU_FLAGS_MASK) != CPU_Z80
		|| snd->memory_write != sound_writemem
		|| !((snd->memory_read == sound_readmem && snd->port_write == sound_writeport)
			|| (snd->memory_read == sound_readmem_7759 && snd->port_write == sound_writeport_7759))
		|| snd->port_read != sound_readport
		|| !z80_shared_SZHVC_add || !z80_shared_SZHVC_sub
		|| z80_get_context(NULL) > sizeof(ctx) || !s16snd_start_task())
		return;
	s16snd_7759 = snd->memory_read == sound_readmem_7759;
	s16snd_mem = memory_region(REGION_CPU2);
	s16snd_cpf = S16SND_CLOCK / Machine->drv->frames_per_second;
	s16snd_frame_len = 1.0f / Machine->drv->frames_per_second;
	s16snd_t0 = timer_get_time();
	sndz80_rm = s16snd_rm; sndz80_wm = s16snd_wm; sndz80_in = s16snd_in; sndz80_out = s16snd_out;
	z80snd_reset(NULL);
	z80_get_context(ctx);
	z80snd_set_context(ctx);
	z80snd_set_irq_callback(s16snd_irq_ack);
	z80snd_idle_enable = 0;
	z80snd_ICount = 0;
	s16snd_latch = soundlatch_r(0);
	s16snd_now = 0;
	timer_suspendcpu(1, 1, SUSPEND_REASON_DISABLE);
	ym2151_timer_hook = s16snd_timer;
	if (s16snd_7759)
		upd7759_timer_hook = s16snd_dac_timer;
	s16snd_core1 = 1;
	YM2151_timers_to_host();
	if (s16snd_7759)
		UPD7759_timers_to_host();
	mamego_sndboard_pre = s16snd_pre;
	mamego_sndboard_post = s16snd_post;
	mamego_state_hook = s16snd_state_hook;
	printf("sys16: sound board (Z80 + YM2151%s) on the second core\n", s16snd_7759 ? " + uPD7759" : "");
}

void s16snd_disable(void)
{
	if (!s16snd_core1)
		return;
	s16snd_wait();
	mamego_sndboard_pre = mamego_sndboard_post = 0;
	mamego_state_hook = 0;
	ym2151_timer_hook = 0;
	upd7759_timer_hook = 0;
	s16snd_core1 = 0;
}

static void s16snd_try_enable(void)
{
	static int tried;
	if (!tried && cpu_getcurrentframe() > 2)
	{
		tried = 1;
		s16snd_enable();
	}
}
