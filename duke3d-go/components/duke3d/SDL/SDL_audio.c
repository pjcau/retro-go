#include "SDL_audio.h"
#include <rg_system.h>

SDL_AudioSpec as;
bool paused = true;
bool locked = false;
SemaphoreHandle_t xSemaphoreAudio = NULL;

// The game's callback fills SAMPLECOUNT mono 16-bit samples at SAMPLERATE
// (11025 Hz, rg_system_init rate); retro-go wants stereo frames and blocks
// in rg_audio_submit until they are queued, which paces this task.
static void updateTask(void *arg)
{
  static int16_t mono[SAMPLECOUNT];
  static rg_audio_sample_t stereo[SAMPLECOUNT];
  while (1)
  {
    if (!paused && !locked && as.callback)
    {
      memset(mono, 0, sizeof(mono));
      (*as.callback)(NULL, (Uint8 *)mono, SAMPLECOUNT * SAMPLESIZE);
      for (int i = 0; i < SAMPLECOUNT; i++)
        stereo[i].left = stereo[i].right = mono[i];
      rg_audio_submit(stereo, SAMPLECOUNT);
    }
    else
      rg_task_delay(5);
  }
}

void SDL_AudioInit()
{
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	SDL_AudioInit();
	memset(obtained, 0, sizeof(SDL_AudioSpec));
	obtained->freq = SAMPLERATE;
	obtained->format = 16;
	obtained->channels = 1;
	obtained->samples = SAMPLECOUNT;
	obtained->callback = desired->callback;
	memcpy(&as,obtained,sizeof(SDL_AudioSpec));

	static bool started = false;
	if (!started)
	{
		rg_task_create("dukeAudio", &updateTask, NULL, 4 * 1024, RG_TASK_PRIORITY_5, 1);
		started = true;
	}
	printf("audio task started\n");
	return 0;
}

void SDL_PauseAudio(int pause_on)
{
	paused = pause_on;
}

void SDL_CloseAudio(void)
{

}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels, int src_rate, Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
	cvt->len_mult = 1;
	return 0;
}

// The ODROID-GO port turned each sample into two words for the ESP32's
// internal DAC in differential mode -- twice the bytes it was given. Here the
// mixer's signed 16-bit mono samples go to retro-go as they are, and that
// doubling wrote 512 bytes past updateTask's mono[] buffer into FatFs'
// volume table: the first file access after the video init crashed.
int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
	(void)cvt;
	return 0;
}

void SDL_LockAudio(void)
{
	locked = true;
	//if( xSemaphoreAudio != NULL )
	//	xSemaphoreTake( xSemaphoreAudio, 100 );
}

void SDL_UnlockAudio(void)
{
    locked = false;
	//if( xSemaphoreAudio != NULL )
	//	 xSemaphoreGive( xSemaphoreAudio );
}

