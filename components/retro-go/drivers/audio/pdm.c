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
} state;

static bool driver_init(int device, int sample_rate)
{
    state.last_error = NULL;
    state.chan = NULL;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &state.chan, NULL);
    if (ret == ESP_OK)
    {
        ret = i2s_channel_init_pdm_tx_mode(state.chan, &(i2s_pdm_tx_config_t){
            .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .clk = I2S_GPIO_UNUSED, // not routed; the analog filter needs no clock
                .dout = RG_GPIO_SND_I2S_DATA,
            },
        });
    }
    if (ret == ESP_OK)
        ret = i2s_channel_enable(state.chan);
    if (ret != ESP_OK)
        state.last_error = esp_err_to_name(ret);
    return state.last_error == NULL;
}

static bool driver_deinit(void)
{
    if (state.chan)
    {
        i2s_channel_disable(state.chan);
        i2s_del_channel(state.chan);
        state.chan = NULL;
    }
    gpio_reset_pin(RG_GPIO_SND_I2S_DATA);
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    float volume = state.muted ? 0.f : (state.volume * 0.01f);
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
    i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate);
    return i2s_channel_disable(state.chan) == ESP_OK
        && i2s_channel_reconfig_pdm_tx_clock(state.chan, &clk_cfg) == ESP_OK
        && i2s_channel_enable(state.chan) == ESP_OK;
}

static bool driver_set_mute(bool mute)
{
    state.muted = mute;
    return true;
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    return true;
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
