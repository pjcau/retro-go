/* neoframes: run a mame-go game on the PC and hash (or dump) its frames.
 *
 * The x86 harness of the Arcade 60 fps plan: the same mame2000 code the board
 * runs, driven as a libretro core, with a deterministic input script. Two runs
 * of the same build print the same hashes; a renderer change (band rendering,
 * V1) must keep them identical to the full-frame build's. Built and driven by
 * scripts/neogeo_frames.py.
 *
 *     neoframes <sysdir> <game.zip> <frames> [options]
 *       --every N      hash every N-th frame (default 1); frame 0 is the first run
 *       --load FILE    load this save state (raw retro_serialize data, as the
 *                      board writes it) after the first frame
 *       --save FILE@N  save the state after frame N (same format)
 *       --dump DIR@N   write frame N as DIR/frame-N.ppm (RGB565 -> RGB888)
 *       --input MODE   bench (default: the MAMEBENCH script of main.c) or
 *                      attract (coin at frame 120, start at frame 200, then bench)
 *                      or none
 *
 * Output: one "FRAME <n> <w>x<h> hash <hex>" line per hashed frame, and at the
 * end "FRAMES <n> all <hex>" with the hash of every hashed frame's hash.
 * The sound: "AUDIO <n> hash <hex>" on the same frames, the running hash of
 * every sample delivered so far, and at the end "AUDIO all <hex> samples <n>
 * nonzero <n>" (a run that is silent proves nothing about the mixer), and every
 * 300 frames "LEVEL <n> rms <x> peak <p> samples <n>". "--rate 16000" makes
 * the chips render at that rate (the board's AUDIO_MIX_HZ).
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libretro.h"

static const char *sysdir;
static unsigned frame_no, hash_every = 1;
static uint32_t last_hash, all_hash = 2166136261u;
static int input_mode = 0;              /* 0 bench, 1 attract, 2 none */
static int quiet;                       /* the frame run before a state load is not hashed */
static const char *dump_dir; static unsigned dump_at = ~0u;
static uint16_t joy;                    /* libretro joypad bits of the current frame */

static const char *sample_rate = "32000";     /* --rate N: the rate the chips render at */
static bool env(unsigned cmd, void *data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = sysdir;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		struct retro_variable *v = data;
		if (!strcmp(v->key, "mame2000-sample_rate")) { v->value = sample_rate; return true; }
		if (!strcmp(v->key, "mame2000-frameskip_type")) { v->value = "disabled"; return true; }
		return false;
	}
	default:
		return false;
	}
}

static void dump_ppm(const uint16_t *d, unsigned w, unsigned h, size_t pitch)
{
	char path[1024];
	FILE *fp;
	snprintf(path, sizeof path, "%s/frame-%u.ppm", dump_dir, frame_no);
	fp = fopen(path, "wb");
	if (!fp) { perror(path); return; }
	fprintf(fp, "P6\n%u %u\n255\n", w, h);
	for (unsigned y = 0; y < h; y++)
	{
		const uint16_t *p = (const uint16_t *)((const uint8_t *)d + y * pitch);
		for (unsigned x = 0; x < w; x++)
		{
			unsigned char rgb[3] = { (p[x] >> 11) << 3, ((p[x] >> 5) & 63) << 2, (p[x] & 31) << 3 };
			fwrite(rgb, 1, 3, fp);
		}
	}
	fclose(fp);
}

static void video(const void *d, unsigned w, unsigned h, size_t pitch)
{
	if (!d || quiet) return;             /* duplicate frame, or the pre-load frame */
	if (frame_no % hash_every == 0)
	{
		uint32_t x = 2166136261u;        /* the FNV of main.c's video_cb */
		for (unsigned y = 0; y < h; y++)
		{
			const uint16_t *p = (const uint16_t *)((const uint8_t *)d + y * pitch);
			for (unsigned i = 0; i < w; i++)
				x = (x ^ p[i]) * 16777619u;
		}
		last_hash = x;
		all_hash = (all_hash ^ x) * 16777619u;
		printf("FRAME %u %ux%u hash %08x\n", frame_no, w, h, x);
	}
	if (dump_dir && frame_no == dump_at)
		dump_ppm(d, w, h, pitch);
}
static void sample(int16_t l, int16_t r) {}
static double audio_sq;                       /* sum of squares since the last LEVEL line */
static unsigned long audio_n, audio_peak;
static uint32_t audio_hash = 2166136261u;
static unsigned long audio_samples, audio_nonzero;
static size_t batch(const int16_t *d, size_t n)
{
	if (quiet) return n;
	for (size_t i = 0; i < n * 2; i++)       /* interleaved left, right */
	{
		int v = d[i] < 0 ? -d[i] : d[i];
		audio_hash = (audio_hash ^ (uint16_t)d[i]) * 16777619u;
		audio_sq += (double)d[i] * d[i];
		audio_n++;
		if ((unsigned long)v > audio_peak) audio_peak = v;
		audio_nonzero += d[i] != 0;
	}
	audio_samples += n * 2;                  /* values, both channels, like the non-zero count */
	return n;
}
static void poll(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
	if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return joy;
	return (joy >> id) & 1;
}

/* the MAMEBENCH script (main.c bench_input): walk right, fire and jump now and then */
static uint16_t bench_input(unsigned f)
{
	uint16_t k = 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	if (f % 20 < 2) k |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
	if (f % 90 < 3) k |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
	if (f % 600 >= 300 && f % 600 < 360) k = (1 << RETRO_DEVICE_ID_JOYPAD_LEFT) | (f % 20 < 2 ? 1 << RETRO_DEVICE_ID_JOYPAD_A : 0);
	return k;
}

static uint16_t script(unsigned f)
{
	switch (input_mode)
	{
	case 1:
		if (f >= 120 && f < 126) return 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;   /* coin */
		if (f >= 200 && f < 206) return 1 << RETRO_DEVICE_ID_JOYPAD_START;
		if (f < 300) return 0;
		return bench_input(f - 300);
	case 2:
		return 0;
	case 3:                                  /* the board's MAMEBENCH=2 (main.c BENCH_*_AT) */
		if (f >= 600 && f < 606) return 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;   /* coin */
		if (f >= 720 && f < 726) return 1 << RETRO_DEVICE_ID_JOYPAD_START;
		if (f < 900) return 0;
		return bench_input(f - 900);
	default:
		return bench_input(f);
	}
}

/* the board keeps big regions in its flash partition; here they stay in RAM */
unsigned char *mamego_flash_store(const unsigned char *data, size_t len, size_t *offset) { return NULL; }

static int state_io(const char *path, int save)
{
	size_t size = retro_serialize_size();
	void *buf = malloc(size);
	FILE *fp = fopen(path, save ? "wb" : "rb");
	int ok = buf && fp;
	if (ok && save) ok = retro_serialize(buf, size) && fwrite(buf, 1, size, fp) == size;
	if (ok && !save) ok = fread(buf, 1, size, fp) == size && retro_unserialize(buf, size);
	if (fp) fclose(fp);
	free(buf);
	fprintf(stderr, "neoframes: %s %s (%zu bytes): %s\n", save ? "saved" : "loaded", path, size, ok ? "ok" : "FAILED");
	return ok;
}

int main(int argc, char **argv)
{
	struct retro_game_info game = {0};
	const char *load = NULL, *save = NULL;
	unsigned frames, save_at = ~0u;
	char *at;

	if (argc < 4)
	{
		fprintf(stderr, "usage: %s <sysdir> <game.zip> <frames> [--every N] [--load FILE] [--save FILE@N] [--dump DIR@N] [--input bench|attract|none|play]\n", argv[0]);
		return 2;
	}
	sysdir = argv[1];
	game.path = argv[2];
	frames = atoi(argv[3]);
	for (int i = 4; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--every")) hash_every = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--load")) load = argv[i + 1];
		else if (!strcmp(argv[i], "--save") && (at = strrchr(argv[i + 1], '@'))) { *at = 0; save = argv[i + 1]; save_at = atoi(at + 1); }
		else if (!strcmp(argv[i], "--dump") && (at = strrchr(argv[i + 1], '@'))) { *at = 0; dump_dir = argv[i + 1]; dump_at = atoi(at + 1); }
		else if (!strcmp(argv[i], "--rate")) sample_rate = argv[i + 1];
		else if (!strcmp(argv[i], "--input")) input_mode = !strcmp(argv[i + 1], "attract") ? 1 : !strcmp(argv[i + 1], "none") ? 2 : !strcmp(argv[i + 1], "play") ? 3 : 0;
		else { fprintf(stderr, "neoframes: bad option %s\n", argv[i]); return 2; }
	}
	if (!hash_every) hash_every = 1;

	retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample(sample);
	retro_set_audio_sample_batch(batch);
	retro_set_input_poll(poll);
	retro_set_input_state(state);
	retro_init();
	if (!retro_load_game(&game))
	{
		fprintf(stderr, "neoframes: %s did not load\n", argv[2]);
		return 1;
	}
	if (load)
	{
		quiet = 1;                       /* the first frame is only there to start the video */
		joy = 0;
		for (int i = 0; i < 6; i++)  /* as the board: the sound board's state exists a few frames in */
			retro_run();
		if (!state_io(load, 0)) return 1;
		quiet = 0;
		frame_no = 0;
	}
	for (; frame_no < frames; frame_no++)
	{
		joy = script(frame_no);
		retro_run();
		if (frame_no % hash_every == 0)
			printf("AUDIO %u hash %08x\n", frame_no, audio_hash);
		if (frame_no % 300 == 299)
		{
			/* the loudness of the last 300 frames: compares two sample rates on the same scene */
			printf("LEVEL %u rms %.1f peak %lu samples %lu\n", frame_no + 1, audio_n ? sqrt(audio_sq / audio_n) : 0.0, audio_peak, audio_n);
			audio_sq = 0; audio_n = 0; audio_peak = 0;
		}
		if (save && frame_no == save_at && !state_io(save, 1)) return 1;
	}
	printf("FRAMES %u all %08x\n", frames, all_hash);
	printf("AUDIO all %08x samples %lu nonzero %lu\n", audio_hash, audio_samples, audio_nonzero);
	return 0;
}
