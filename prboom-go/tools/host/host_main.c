/* Headless host harness for prboom-go's engine.
 *
 * Runs the engine on a PC with no video, no audio and no input, so the zone
 * allocator can be watched over thousands of tics. Used to tell a true leak
 * from the lump cache growing by design.
 *
 * Build: see build.sh. Run: ./doomhost <iwad> [tics]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "doomtype.h"
#include "doomstat.h"
#include "doomdef.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_main.h"
#include "m_argv.h"
#include "m_fixed.h"
#include "m_misc.h"
#include "r_draw.h"
#include "r_fps.h"
#include "s_sound.h"
#include "sounds.h"
#include "mus2mid.h"
#include "oplplayer.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

int snd_card = 1, mus_card = 1;          /* silent sink, but the real code paths */
int snd_samplerate = 16000;
int current_palette = 0;

static byte host_screen[MAX_SCREENWIDTH * MAX_SCREENHEIGHT];
static int64_t start_us;
static int64_t fake_tic;                 /* the clock the engine sees */

static int64_t now_us(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* -- retro-go helpers the fork calls ------------------------------------- */
void rg_gui_draw_loading(int percent) { (void)percent; }
size_t rg_storage_fread_raw(void *buffer, size_t length, FILE *fp)
{
  return fread(buffer, 1, length, fp);
}

/* -- video -------------------------------------------------------------- */
void I_StartFrame(void) {}
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void) {}
bool I_StartDisplay(void) { return true; }
void I_EndDisplay(void) {}
void I_SetPalette(int pal)
{
  void *palette = V_BuildPalette(pal, 16);
  Z_Free(palette);
  current_palette = pal;
}
void I_InitGraphics(void)
{
  for (int i = 0; i < 3; i++)
  {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].byte_pitch = SCREENWIDTH;
  }
  screens[0].data = host_screen;
  screens[0].not_on_heap = true;

  screens[4].width = SCREENWIDTH;
  screens[4].height = (ST_SCALED_HEIGHT + 1);
  screens[4].byte_pitch = SCREENWIDTH;
}
void I_UpdateVideoMode(void) {}
void I_ShutdownGraphics(void) {}

/* -- timing ------------------------------------------------------------- */
/* The engine's clock runs as fast as the renderer: one tic per I_GetTime
 * call pair is not enough, so we advance a fake clock by one tic every time
 * the loop asks, which makes the run deterministic and as fast as the CPU. */
void host_tick(void);
/* The engine's clock: one tic per call, so the run is deterministic and as
 * fast as the CPU (the display and input hooks do nothing). */
int I_GetTimeMS(void) { return (int)(fake_tic * 1000 / TICRATE); }
int I_GetTime(void) { host_tick(); return (int)fake_tic++; }
void I_uSleep(unsigned long usecs) { (void)usecs; }
void I_SafeExit(int rc) { exit(rc); }
const char *I_DoomExeDir(void) { return "."; }
const char *I_SigString(char *buf, size_t sz, int signum)
{
  snprintf(buf, sz, "signal %d", signum);
  return buf;
}

/* -- sound: the same glue as prboom-go/main/main.c, with a silent sink ----
 * The mixer, the OPL player and mus2mid are the paths most likely to leak,
 * and they are the fork's own code, so the harness runs them for real. */
#define NUM_MIX_CHANNELS 8

typedef struct {
    uint16_t unused1, samplerate, length, unused2;
    byte samples[];
} doom_sfx_t;

typedef struct {
    const doom_sfx_t *sfx;
    size_t pos;
    float factor;
    int starttic;
} channel_t;

static channel_t channels[NUM_MIX_CHANNELS];
static const doom_sfx_t *sfx[NUMSFX];
static const music_player_t *music_player = &opl_synth_player;
static bool musicPlaying;
static int16_t mixbuffer[(16000 / TICRATE + 1) * 2];

void I_UpdateSoundParams(int h, int v, int s, int p) { (void)h; (void)v; (void)s; (void)p; }

int I_StartSound(int sfxid, int channel, int vol, int sep, int pitch, int priority)
{
  int oldest = gametic, slot = 0;
  (void)channel; (void)vol; (void)sep; (void)pitch; (void)priority;

  if (!sfx[sfxid])
    return -1;
  for (int i = 0; i < NUM_MIX_CHANNELS; i++)
  {
    if (channels[i].sfx == NULL) { slot = i; break; }
    else if (channels[i].starttic < oldest) { slot = i; oldest = channels[i].starttic; }
  }
  channels[slot].sfx = sfx[sfxid];
  channels[slot].factor = (float)channels[slot].sfx->samplerate / snd_samplerate;
  channels[slot].pos = 0;
  return slot;
}
void I_StopSound(int handle) { if (handle < NUM_MIX_CHANNELS && handle >= 0) channels[handle].sfx = NULL; }
bool I_SoundIsPlaying(int handle) { (void)handle; return false; }
bool I_AnySoundStillPlaying(void)
{
  for (int i = 0; i < NUM_MIX_CHANNELS; i++)
    if (channels[i].sfx) return true;
  return false;
}
void I_InitSound(void)
{
  for (int i = 1; i < NUMSFX; i++)
    if (S_sfx[i].lumpnum != -1)
      sfx[i] = W_CacheLumpNum(S_sfx[i].lumpnum);
  music_player->init(snd_samplerate);
  music_player->setvolume(snd_MusicVolume);
}
void I_ShutdownSound(void) { music_player->shutdown(); }
void I_SetChannels(void) {}
/* I_RegisterSong returns an int, which is a pointer on the 32-bit ESP32 but
 * truncates on a 64-bit host, so the host keeps the pointers in a table. */
static void *song_handles[64];
static int num_song_handles;
static void *song_ptr(int handle)
{
  return (handle > 0 && handle <= num_song_handles) ? song_handles[handle - 1] : NULL;
}
void I_PlaySong(int handle, int looping) { if (song_ptr(handle)) { music_player->play(song_ptr(handle), looping); musicPlaying = true; } }
void I_PauseSong(int handle) { (void)handle; music_player->pause(); musicPlaying = false; }
void I_ResumeSong(int handle) { (void)handle; music_player->resume(); musicPlaying = true; }
void I_StopSong(int handle) { (void)handle; music_player->stop(); musicPlaying = false; }
void I_UnRegisterSong(int handle) { if (song_ptr(handle)) music_player->unregistersong(song_ptr(handle)); }
int I_RegisterSong(const void *data, size_t len)
{
  uint8_t *mid = NULL;
  size_t midlen;
  int handle;

  const void *song;
  if (mus2mid(data, len, &mid, &midlen, 64) == 0)
    song = music_player->registersong(mid, midlen);
  else
    song = music_player->registersong(data, len);

  free(mid);

  if (!song)
    return 0;
  if (num_song_handles == 64)
    num_song_handles = 0;              /* the harness only ever holds one */
  song_handles[num_song_handles++] = (void *)song;
  handle = num_song_handles;
  return handle;
}
void I_SetMusicVolume(int volume) { music_player->setvolume(volume); }
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_UpdateMusic(void) {}

/* What the sound task does on the board, once per tic instead of in a task. */
static void host_mix(void)
{
  if (musicPlaying && snd_MusicVolume > 0)
    music_player->render((void *)mixbuffer, sizeof(mixbuffer) / 4);
}

/* -- input (none) ------------------------------------------------------- */
void I_StartTic(void) {}
void I_Init(void)
{
  snd_channels = NUM_MIX_CHANNELS;
  snd_samplerate = 16000;
  snd_MusicVolume = 15;
  snd_SfxVolume = 15;
  usegamma = 0;
}

/* A hash of the frame last drawn. In warp mode the run is deterministic (no
 * input, DOOM's own PRNG), so these are reproducible, and a change that is
 * meant to be invisible must leave them all alone. */
static uint32_t host_frame_hash(void)
{
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof(host_screen); i++)
    h = (h ^ host_screen[i]) * 16777619u;
  return h;
}

/* -- the harness -------------------------------------------------------- */
static const char *host_argv[16];
static int host_tics = 20000;
static int host_report_every = 1000;
static int host_warp;                    /* tics to spend in each map, 0 = off */
static int host_map;

/* One tic per D_DoomLoop iteration: the engine runs as fast as the CPU. */
void host_tick(void)
{
  static int reported;
  int tic = (int)fake_tic;

  host_mix();

  /* "warp" mode: E1M1..E1M9 over and over, reporting just before each load,
   * which is what shows what a level load leaves behind. */
  if (host_warp && tic >= (host_map + 1) * host_warp)
  {
    char where[64];
    snprintf(where, sizeof(where), "load=%d map=E1M%d tic=%d frame=%08x",
             host_map + 1, host_map % 9 + 1, tic, host_frame_hash());
    Z_LogStats(where);
    fflush(stdout);
    host_map++;
    G_DeferedInitNew(sk_medium, 1, host_map % 9 + 1);
  }

  if (!host_warp && tic / host_report_every != reported)
  {
    reported = tic / host_report_every;
    char where[64];
    snprintf(where, sizeof(where), "tic=%d gametime=%.0fs wall=%.0fs map=%d", tic,
             (float)tic / TICRATE, (float)(now_us() - start_us) / 1000000.f, gamemap);
    Z_LogStats(where);
    fflush(stdout);
  }

  if (tic >= host_tics)
  {
    Z_LogStats("final");
    exit(0);
  }
}

int main(int argc, char **argv)
{
  const char *iwad = argc > 1 ? argv[1] : "doom1.wad";
  if (argc > 2)
    host_tics = atoi(argv[2]);
  if (argc > 3)
    host_report_every = atoi(argv[3]);

  start_us = now_us();
#ifdef __APPLE__
  /* The site addresses are runtime addresses; this is what atos needs. */
  printf("host image slide: 0x%lx\n", (unsigned long)_dyld_get_image_vmaddr_slide(0));
#endif

  SCREENWIDTH = 320;
  SCREENHEIGHT = 200;

  myargv = host_argv;
  myargc = 0;
  host_argv[myargc++] = "doom";
  host_argv[myargc++] = "-iwad";
  host_argv[myargc++] = iwad;
  /* Without "attract": play one demo (continuous play on a single map).
   * With it: the title screen's demo loop, which reloads a level every
   * couple of minutes - that is where a per-level leak shows. */
  /* "attract": the title screen's demo loop, which reloads a level every
   *   couple of minutes.
   * "warp <tics>": E1M1..E1M9 over and over, <tics> in each - what a level
   *   load leaves behind, measured per load.
   * Neither: one demo, continuous play on a single map. */
  if (argc > 4 && !strcmp(argv[4], "warp"))
    host_warp = argc > 5 ? atoi(argv[5]) : 700;
  else if (strcmp(argc > 4 ? argv[4] : "", "attract"))
  {
    host_argv[myargc++] = "-playdemo";
    host_argv[myargc++] = "demo1";
  }
  host_argv[myargc] = NULL;

  Z_Init();
  D_DoomMain();
  return 0;
}
