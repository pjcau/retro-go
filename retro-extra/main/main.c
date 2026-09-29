#include "shared.h"

// retro-extra: Neo Geo Pocket (RACE); Atari 2600 removed 2026-09-29 (not needed) — see CMakeLists.txt

void app_main(void)
{
    rg_app_t *app = rg_system_init(AUDIO_SAMPLE_RATE, NULL, NULL);

    RG_LOGI("configNs=%s", app->configNs);

    if (strcmp(app->configNs, "ngp") == 0 || strcmp(app->configNs, "ngpc") == 0)
        ngp_main();
    else
        RG_PANIC("Unknown app for retro-extra");

    RG_PANIC("Never reached");
}
