#include "SDL_audio.h"
#include <rg_system.h>
#include <string.h>
#include <stdlib.h>

static SDL_AudioSpec as;
static bool paused = true;
static rg_audio_sample_t *audio_buffer = NULL;
static uint8_t *sdl_mix_buffer = NULL;
static SemaphoreHandle_t audio_mutex = NULL;

// The PDM sink runs every app at 32 kHz (lower rates crackle on this board).
// Wolf mixes at half that rate to keep the OPL synthesis cheap; each sample is
// doubled with linear interpolation, like prboom-go and duke3d-go.
#define OUTPUT_RATE 32000

static void audio_task(void *arg)
{
    int16_t prev = 0;
    while (1)
    {
        if (!paused && as.callback)
        {
            int bytes = as.samples * as.channels * 2;
            SDL_LockAudio();
            memset(sdl_mix_buffer, 0, bytes);
            as.callback(as.userdata, sdl_mix_buffer, bytes);
            SDL_UnlockAudio();

            bool doubled = as.freq * 2 == OUTPUT_RATE;
            int16_t *src = (int16_t *)sdl_mix_buffer;
            int out_samples = 0;
            for (int i = 0; i < as.samples; i++) {
                int16_t s = as.channels == 2 ? (src[i * 2] + src[i * 2 + 1]) / 2 : src[i];
                if (doubled) {
                    int16_t mid = (prev + s) / 2;
                    audio_buffer[out_samples].left = mid;
                    audio_buffer[out_samples].right = mid;
                    out_samples++;
                }
                audio_buffer[out_samples].left = s;
                audio_buffer[out_samples].right = s;
                out_samples++;
                prev = s;
            }
            rg_audio_submit(audio_buffer, out_samples);
        }
        else
        {
            vTaskDelay(10);
        }
    }
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    as = *desired;
    if (obtained) *obtained = *desired;

    if (audio_mutex == NULL) {
        audio_mutex = xSemaphoreCreateMutex();
    }

    audio_buffer = (rg_audio_sample_t *)malloc(as.samples * 2 * sizeof(rg_audio_sample_t));
    sdl_mix_buffer = (uint8_t *)malloc(as.samples * 4); // Enough for stereo 16-bit

    // Wolf's OPL synthesis and channel mixing must complete before the next
    // buffer deadline. Keep it above Retro-Go's display task so sustained
    // rendering cannot starve audio.
    rg_task_create("audio_task", audio_task, NULL, 3072, RG_TASK_PRIORITY_7, 1);

    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    paused = pause_on;
}

void SDL_CloseAudio(void)
{
    paused = true;
}

void SDL_LockAudio(void)
{
    if (audio_mutex) {
        xSemaphoreTake(audio_mutex, portMAX_DELAY);
    }
}

void SDL_UnlockAudio(void)
{
    if (audio_mutex) {
        xSemaphoreGive(audio_mutex);
    }
}

void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume)
{
    // Basic mix
    int16_t *s = (int16_t *)src;
    int16_t *d = (int16_t *)dst;
    int count = len / 2;
    
    for (int i = 0; i < count; i++) {
        int32_t mix = d[i] + (s[i] * volume / SDL_MIX_MAXVOLUME);
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        d[i] = (int16_t)mix;
    }
}
