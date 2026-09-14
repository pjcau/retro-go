#include <rg_system.h>

#include "game.h"

// Duke Nukem 3D (Chocolate Duke3D via jkirsons' ESP32 port) for esp32-emu-turbo.
// The ROM picked in the launcher is the .grp file; the game runs on its own
// task (deep stack) and SDL_video.c / SDL_event.c bridge it to retro-go.

static void dukeTask(void *pvParameters)
{
    char *argv[] = {"duke3d", "/nm", NULL};
    main(2, argv);
    rg_system_exit();
}

void app_main(void)
{
    const rg_handlers_t handlers = {0};
    rg_app_t *app = rg_system_init(11025, &handlers, NULL);
    RG_LOGI("Duke3D start, rom=%s", app->romPath ? app->romPath : "(none)");
    rg_task_create("dukeTask", &dukeTask, NULL, 16 * 1024, RG_TASK_PRIORITY_5, 0);
    // The main task idles; the game task owns the frame loop.
    while (1)
        rg_task_delay(1000);
}
