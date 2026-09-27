#include "SDL_audio.h"
#include <rg_system.h>
#include <rg_audio.h>
#include <string.h>

#define AUDIO_SOURCE_FRAMES 256
#define AUDIO_SINK_RATE 32000  /* every app feeds the PDM sink at 32 kHz */
#define AUDIO_STEP ((uint32_t)(((uint64_t)TYRIAN_SAMPLERATE << 16) / AUDIO_SINK_RATE))
#define AUDIO_OUTPUT_FRAMES (AUDIO_SOURCE_FRAMES * AUDIO_SINK_RATE / TYRIAN_SAMPLERATE + 4)

static SDL_AudioSpec active_as;
static volatile bool audio_running = false;
static volatile bool audio_task_done = false;
static volatile bool audio_paused = true;
static volatile bool audio_in_submit = false;
static SemaphoreHandle_t audio_mutex = NULL;

static int16_t mono_buffer[AUDIO_SOURCE_FRAMES];
static rg_audio_frame_t stereo_buffer[AUDIO_OUTPUT_FRAMES];
static uint32_t resample_pos; /* 16.16 position in the current source block */
static int16_t resample_prev; /* last sample of the previous block */

static void audio_update_task(void *arg)
{
    while (audio_running)
    {
        if (audio_paused || rg_audio_get_mute() || !active_as.callback)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        memset(mono_buffer, 0, sizeof(mono_buffer));
        (*active_as.callback)(active_as.userdata, (Uint8 *)mono_buffer, sizeof(mono_buffer));

        // OpenTyrian's native 11,025 Hz mix is sufficient for its original
        // assets; stretch the completed mix to the 32 kHz sink with linear
        // interpolation instead of running every SFX channel and the OPL
        // synth at 32 kHz. Position p interpolates source[p-1] -> source[p].
        int count = 0;
        for (uint32_t idx; (idx = resample_pos >> 16) < AUDIO_SOURCE_FRAMES; resample_pos += AUDIO_STEP)
        {
            int a = idx ? mono_buffer[idx - 1] : resample_prev;
            int b = mono_buffer[idx];
            int16_t v = a + (((b - a) * (int32_t)(resample_pos & 0xFFFF)) >> 16);
            stereo_buffer[count++] = (rg_audio_frame_t){ v, v };
        }
        resample_pos -= AUDIO_SOURCE_FRAMES << 16;
        resample_prev = mono_buffer[AUDIO_SOURCE_FRAMES - 1];

        if (audio_running && !audio_paused && !rg_audio_get_mute())
        {
            audio_in_submit = true;
            rg_audio_submit(stereo_buffer, count);
            audio_in_submit = false;
        }
    }

    audio_task_done = true;
    return;
}

void SDL_AudioInit(void)
{
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (obtained)
    {
        *obtained = *desired;
        obtained->freq = TYRIAN_SAMPLERATE;
        obtained->format = AUDIO_S16SYS;
        obtained->channels = 1;
        obtained->samples = AUDIO_SOURCE_FRAMES;
    }

    active_as = *desired;
    active_as.freq = TYRIAN_SAMPLERATE;
    active_as.format = AUDIO_S16SYS;
    active_as.samples = AUDIO_SOURCE_FRAMES;

    if (!audio_mutex)
        audio_mutex = xSemaphoreCreateMutex();

    rg_audio_set_mute(false);

    audio_running = true;
    audio_task_done = false;
    audio_paused = true;

    // Above rg_display/rg_input (priority 6, also on core 1) like wolf3d-go: at
    // priority 2 the display task's scaling starved the mixer and the sound skipped.
    rg_task_create("audio_task", audio_update_task, NULL, 16 * 1024, RG_TASK_PRIORITY_7, 1);
    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    audio_paused = pause_on ? true : false;
    if (pause_on)
    {
        while (audio_in_submit)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void SDL_CloseAudio(void)
{
    rg_audio_set_mute(true);
    if (!audio_running)
        return;

    audio_running = false;
    audio_paused = true;

    for (int i = 0; i < 50 && !audio_task_done; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (audio_mutex)
    {
        vSemaphoreDelete(audio_mutex);
        audio_mutex = NULL;
    }
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels, int src_rate, Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    if (cvt)
        cvt->len_mult = 1;
    return 0;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    return 0;
}

void SDL_LockAudio(void)
{
    if (audio_mutex)
        xSemaphoreTake(audio_mutex, portMAX_DELAY);
}

void SDL_UnlockAudio(void)
{
    if (audio_mutex)
        xSemaphoreGive(audio_mutex);
}
