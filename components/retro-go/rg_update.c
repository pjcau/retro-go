/* Firmware update from the SD card (esp32-emu-turbo, roadmap Phase 6).
 *
 * Put <partition label>.bin files (the app images the build writes, e.g.
 * mame-go.bin) in RG_UPDATE_DIR. At boot:
 *  - the launcher writes every app image but its own into its partition,
 *    checks it (esp_image_verify) and renames the file to .done;
 *  - launcher.bin cannot be written by the running launcher: the launcher
 *    hands over to another app, whose boot writes the launcher partition and
 *    switches back.
 * A partition table change still needs a USB flash (the layout moves). An
 * image that does not fit its partition, or is not an image, is left alone
 * and reported; a power cut while writing leaves that app broken until the
 * file is put back, never the app that does the writing. */
#include "rg_system.h"

#ifdef ESP_PLATFORM
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define RG_UPDATE_DIR RG_BASE_PATH "/update"
#define UPDATE_CHUNK (16 * 1024)

static void update_message(const char *label, int percent)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Updating %s... %d%%", label, percent);
    rg_gui_draw_message(msg);
}

/* one image into one partition; true if written and verified */
static bool write_image(const esp_partition_t *part, const char *path)
{
    struct stat st;
    FILE *fp = NULL;
    uint8_t *buf = NULL;
    bool ok = false;
    size_t done = 0;

    if (stat(path, &st) != 0 || st.st_size < 32 || (size_t)st.st_size > part->size)
    {
        RG_LOGE("%s: %ld bytes do not fit partition '%s' (%u)", path, (long)st.st_size, part->label, (unsigned)part->size);
        return false;
    }
    if (!(fp = fopen(path, "rb")) || !(buf = malloc(UPDATE_CHUNK)))
        goto out;
    if (fread(buf, 1, 1, fp) != 1 || buf[0] != ESP_IMAGE_HEADER_MAGIC)
    {
        RG_LOGE("%s: not an app image", path);
        goto out;
    }
    rewind(fp);

    RG_LOGI("Writing %s (%ld bytes) to '%s'", path, (long)st.st_size, part->label);
    update_message(part->label, 0);
    if (esp_partition_erase_range(part, 0, (st.st_size + 4095) & ~4095) != ESP_OK)
        goto out;
    while (done < (size_t)st.st_size)
    {
        size_t n = fread(buf, 1, UPDATE_CHUNK, fp);
        if (!n || esp_partition_write(part, done, buf, n) != ESP_OK)
            goto out;
        done += n;
        if ((done / UPDATE_CHUNK) % 8 == 0)
            update_message(part->label, (int)(done * 100 / st.st_size));
    }

    esp_image_metadata_t meta;
    const esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
    ok = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta) == ESP_OK;
    RG_LOGI("'%s': %s", part->label, ok ? "written and verified" : "VERIFY FAILED");

out:
    if (fp)
        fclose(fp);
    free(buf);
    return ok;
}

static bool apply(const esp_partition_t *part)
{
    char path[RG_PATH_MAX], done[RG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.bin", RG_UPDATE_DIR, part->label);
    if (access(path, F_OK) != 0)
        return false;
    bool ok = write_image(part, path);
    snprintf(done, sizeof(done), "%s/%s.%s", RG_UPDATE_DIR, part->label, ok ? "done" : "failed");
    remove(done);
    rename(path, done);
    return ok;
}

void rg_update_check(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_partition_iterator_t it;
    char path[RG_PATH_MAX];
    int updated = 0;

    if (!running || access(RG_UPDATE_DIR, F_OK) != 0)
        return;

    snprintf(path, sizeof(path), "%s/%s.bin", RG_UPDATE_DIR, RG_APP_LAUNCHER);
    bool launcher_pending = access(path, F_OK) == 0;
    bool in_launcher = strcmp(running->label, RG_APP_LAUNCHER) == 0;

    if (!in_launcher)
    {
        /* another app: it only ever writes the launcher, then goes back to it */
        if (launcher_pending)
        {
            const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, RG_APP_LAUNCHER);
            if (p)
                apply(p);
            rg_system_switch_app(RG_APP_LAUNCHER, NULL, NULL, 0);
        }
        return;
    }

    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it))
    {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address != running->address)
            updated += apply(p);
    }
    esp_partition_iterator_release(it);

    if (launcher_pending)
    {
        /* hand over to the first other app, which writes us and comes back */
        it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
        for (; it; it = esp_partition_next(it))
        {
            const esp_partition_t *p = esp_partition_get(it);
            if (p->address != running->address)
            {
                char label[17];
                snprintf(label, sizeof(label), "%s", p->label);
                esp_partition_iterator_release(it);
                RG_LOGI("Launcher update: handing over to '%s'", label);
                rg_gui_draw_message("Updating the launcher...");
                rg_system_switch_app(label, NULL, NULL, 0);
                return;
            }
        }
        esp_partition_iterator_release(it);
    }
    if (updated)
        rg_gui_alert("Update from SD", "Done. See retro-go/update/ on the card.");
}
#else
void rg_update_check(void) {}
#endif
