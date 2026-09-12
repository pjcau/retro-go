#include "rg_system.h"
#include "rg_audio.h"

#if RG_AUDIO_USE_PDM

#ifndef ESP_PLATFORM
#error "PDM support can only be built inside esp-idf!"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2s_pdm.h>
#include <soc/soc_caps.h>

#if !SOC_I2S_SUPPORTS_PDM_TX
#error "Your chip has no PDM TX! Please set RG_AUDIO_USE_PDM to 0 in your target file."
#endif

// PDM TX: the I2S peripheral sigma-delta modulates 16-bit PCM into a 1-bit
// stream on a single data pin; an analog low-pass after the pin reconstructs
// the audio (on esp32-emu-turbo: GPIO17 -> C22 DC-block -> PAM8403). No
// clock pin is required by the receiving side, so only RG_GPIO_SND_I2S_DATA
// is used. Uses the IDF5 channel API — do not mix with the legacy i2s.c
// driver in the same build (both claim I2S0).

#define DMA_DESC_NUM 4
#define DMA_FRAME_NUM 256
#define SUBMIT_CHUNK 256

static struct {
    const char *last_error;
    i2s_chan_handle_t chan;
    int volume;
    bool muted;
    bool enabled;       // channel running (carrier on the pin)
    int sample_rate;
    int64_t busy_until; // pacing while the channel is off
} state;

// The board has no reconstruction filter between the PDM pin and the PAM8403
// (first-article finding R38-MED-1): an enabled channel emits its 50%-density
// carrier as a loud hiss even when every sample is zero, so zeroing samples
// on mute/volume 0 silences nothing. The channel is therefore only enabled
// while there is something audible to play, and disabled otherwise (pin idle
// LOW, carrier gone). Submissions while the channel is off are paced in time
// exactly like the dummy driver so the emulators keep their speed regulation.
static bool apply_state(void)
{
    bool want = state.chan && !state.muted && state.volume > 0;
    if (want == state.enabled)
        return true;
    esp_err_t ret = want ? i2s_channel_enable(state.chan) : i2s_channel_disable(state.chan);
    if (ret != ESP_OK)
    {
        state.last_error = esp_err_to_name(ret);
        return false;
    }
    state.enabled = want;
    return true;
}

static bool driver_init(int device, int sample_rate)
{
    state.last_error = NULL;
    state.chan = NULL;
    state.enabled = false;
    state.sample_rate = sample_rate;
    state.busy_until = 0;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &state.chan, NULL);
    if (ret == ESP_OK)
    {
        // DAC line mode, not codec line mode: the data pin drives an analog
        // load (RC + amplifier), and this mode is the one IDF documents for
        // that — single line, PDM carrier fixed at 128 x 48 kHz (6.1 MHz,
        // further from the audio band than codec mode's 128 x Fs) and the
        // sigma-delta/filter scaling Espressif tuned for SNR. The PCM
        // consumption rate is still sample_rate, so the pacing is unchanged.
        ret = i2s_channel_init_pdm_tx_mode(state.chan, &(i2s_pdm_tx_config_t){
            .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .clk = I2S_GPIO_UNUSED, // not routed; the analog filter needs no clock
                .dout = RG_GPIO_SND_I2S_DATA,
            },
        });
    }
    if (ret != ESP_OK)
        state.last_error = esp_err_to_name(ret);
    // Not enabled here: rg_audio_init() pushes mute/volume right after init
    // and apply_state() starts the carrier only if they say so.
    return state.last_error == NULL;
}

static bool driver_deinit(void)
{
    if (state.chan)
    {
        if (state.enabled)
            i2s_channel_disable(state.chan);
        i2s_del_channel(state.chan);
        state.chan = NULL;
        state.enabled = false;
    }
    gpio_reset_pin(RG_GPIO_SND_I2S_DATA);
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    if (!state.enabled)
    {
        // Same pacing as drivers/audio/dummy.c: block for the duration this
        // chunk would have taken to play.
        if (state.busy_until > rg_system_timer())
            rg_usleep(state.busy_until - rg_system_timer());
        state.busy_until = rg_system_timer() + (count * (1000000.f / state.sample_rate));
        return true;
    }

    float volume = state.volume * 0.01f;
    int16_t buffer[SUBMIT_CHUNK];
    size_t pos = 0;

    for (size_t i = 0; i < count; ++i)
    {
        // Mono slot: average the stereo frame (speaker is single-ended BTL)
        buffer[pos++] = ((int)frames[i].left + (int)frames[i].right) / 2 * volume;

        if (pos == SUBMIT_CHUNK || i == count - 1)
        {
            size_t written = 0;
            if (i2s_channel_write(state.chan, buffer, pos * sizeof(int16_t),
                                  &written, 1000 / portTICK_PERIOD_MS) != ESP_OK)
                return false;
            pos = 0;
        }
    }
    return true;
}

static bool driver_set_sample_rates(int sample_rate)
{
    if (!state.chan)
        return false;
    i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(sample_rate);
    bool was_enabled = state.enabled;
    if (was_enabled && i2s_channel_disable(state.chan) != ESP_OK)
        return false;
    state.enabled = false;
    if (i2s_channel_reconfig_pdm_tx_clock(state.chan, &clk_cfg) != ESP_OK)
        return false;
    state.sample_rate = sample_rate;
    return was_enabled ? apply_state() : true;
}

static bool driver_set_mute(bool mute)
{
    state.muted = mute;
    return apply_state();
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    return apply_state();
}

static const char *driver_get_error(void)
{
    return state.last_error;
}

const rg_audio_driver_t rg_audio_driver_pdm = {
    .name = "pdm",
    .init = driver_init,
    .deinit = driver_deinit,
    .submit = driver_submit,
    .set_mute = driver_set_mute,
    .set_volume = driver_set_volume,
    .set_sample_rate = driver_set_sample_rates,
    .get_error = driver_get_error,
};

#endif // RG_AUDIO_USE_PDM
