#include "shared.h"

// retro-home: Atari 7800 (ProSystem). The launcher tab that started the app is
// its configNs.

void app_main(void)
{
    rg_app_t *app = rg_system_init(AUDIO_SAMPLE_RATE, NULL, NULL);

    RG_LOGI("configNs=%s", app->configNs);

    if (strcmp(app->configNs, "a78") == 0)
        a78_main();
    else
        RG_PANIC("Unknown app for retro-home");

    RG_PANIC("Never reached");
}
