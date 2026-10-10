/* Host test for gbsp/main/sram_file.h -- the battery-save file's sizes, its
 * atomic replace and the cases that must NOT lose a save.
 *
 *     ./gbsp/test/run.sh
 *
 * The board cannot easily be made to lose power between a remove and a rename,
 * or to leave a zero-length file behind. Here those are three lines each, which
 * is the whole reason the file logic lives in a header with no retro-go in it.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../main/sram_file.h"

#define DIR "/tmp/sram_file_test"
#define SAV DIR "/game.sav"
#define TMP DIR "/game.sav.tmp"
#define FULL (128 * 1024)

static int failures;

static void check(int ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        failures++;
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static void put_file(const char *path, const char *bytes, size_t len)
{
    FILE *fp = fopen(path, "wb");
    assert(fp);
    if (len)
        assert(fwrite(bytes, 1, len, fp) == len);
    fclose(fp);
}

static void fill(unsigned char *p, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)(i * 7 + seed);
}

int main(void)
{
    static unsigned char out[FULL], in[FULL];

    system("rm -rf " DIR " && mkdir -p " DIR);

    /* ---- the size table ------------------------------------------------- */
    puts("sizes:");
    check(sram_backup_size(SRAM_BACKUP_SRAM, 0, 0, FULL) == 32 * 1024, "SRAM is 32 KB");
    check(sram_backup_size(SRAM_BACKUP_FLASH, 1, 0, FULL) == 64 * 1024, "FLASH 1 bank is 64 KB");
    check(sram_backup_size(SRAM_BACKUP_FLASH, 2, 0, FULL) == 128 * 1024, "FLASH 2 banks is 128 KB");
    check(sram_backup_size(SRAM_BACKUP_EEPROM, 0, 1, FULL) == 512, "EEPROM 512 B");
    check(sram_backup_size(SRAM_BACKUP_EEPROM, 0, 16, FULL) == 8 * 1024, "EEPROM 8 KB");
    check(sram_backup_size(3 /* BACKUP_UNKN */, 0, 0, FULL) == FULL, "an unknown kind writes it all");
    check(sram_backup_size(SRAM_BACKUP_FLASH, 0, 0, FULL) == 64 * 1024, "a zero bank count is one bank");

    /* ---- a round trip, at each real size -------------------------------- */
    puts("round trip:");
    const size_t sizes[] = {512, 8 * 1024, 32 * 1024, 64 * 1024, FULL};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
    {
        char what[80];
        fill(out, sizes[i], i + 1);
        memset(in, 0, sizeof(in));
        const bool w = sram_file_write(SAV, out, sizes[i]);
        const size_t n = sram_file_read(SAV, in, sizeof(in));
        snprintf(what, sizeof(what), "%6u bytes: written, read back identical, file is exactly that",
                 (unsigned)sizes[i]);
        check(w && n == sizes[i] && memcmp(out, in, sizes[i]) == 0 &&
              file_size(SAV) == (long)sizes[i], what);
    }
    check(file_size(TMP) == -1, "no .tmp is left behind after a good write");

    /* ---- a reader asking for less than the file holds -------------------- */
    puts("compatibility with the 128 KB files older builds wrote:");
    fill(out, FULL, 9);
    sram_file_write(SAV, out, FULL);
    memset(in, 0, sizeof(in));
    check(sram_file_read(SAV, in, FULL) == FULL && memcmp(out, in, FULL) == 0,
          "a 128 KB save still loads in full");
    memset(in, 0, sizeof(in));
    check(sram_file_read(SAV, in, 8 * 1024) == 8 * 1024 && memcmp(out, in, 8 * 1024) == 0,
          "and loads truncated when the caller offers less room");
    /* the small write that follows must leave a small file, not a padded one */
    fill(out, 512, 3);
    sram_file_write(SAV, out, 512);
    check(file_size(SAV) == 512, "a 512-byte save replaces it without padding to 128 KB");

    /* ---- a power cut between the remove and the rename ------------------- */
    puts("recovery:");
    remove(SAV);
    fill(out, 1024, 5);
    put_file(TMP, (const char *)out, 1024);
    memset(in, 0, sizeof(in));
    const size_t rec = sram_file_read(SAV, in, sizeof(in));
    check(rec == 1024 && memcmp(out, in, 1024) == 0, "a lone .tmp is read as the newer save");
    check(file_size(SAV) == 1024 && file_size(TMP) == -1, "and is put back in place");

    /* ---- files that must be rejected, not believed ----------------------- */
    puts("rejected:");
    put_file(SAV, "", 0);
    memset(in, 0xA5, 16);
    check(sram_file_read(SAV, in, sizeof(in)) == 0, "an empty save reads as nothing");
    check(in[0] == 0xA5, "and does not touch the caller's buffer");

    remove(SAV);
    put_file(TMP, "", 0);
    check(sram_file_read(SAV, in, sizeof(in)) == 0, "an empty .tmp reads as nothing");
    check(file_size(TMP) == -1, "and is removed rather than left to be found again");

    remove(SAV);
    remove(TMP);
    check(sram_file_read(SAV, in, sizeof(in)) == 0, "no save at all reads as nothing");
    check(sram_file_read(NULL, in, sizeof(in)) == 0, "no path reads as nothing");
    check(!sram_file_write(SAV, out, 0), "a zero-length write is refused");
    check(!sram_file_write(NULL, out, 16), "a write with no path is refused");

    /* ---- a write that cannot happen must not destroy the old save -------- */
    puts("a failed write keeps the old save:");
    fill(out, 2048, 11);
    assert(sram_file_write(SAV, out, 2048));
    assert(chmod(DIR, 0500) == 0);   /* no new file may be created here */
    fill(in, 2048, 12);
    const bool w = sram_file_write(SAV, in, 2048);
    assert(chmod(DIR, 0700) == 0);
    check(!w, "the write reports failure");
    memset(in, 0, sizeof(in));
    check(sram_file_read(SAV, in, sizeof(in)) == 2048 && memcmp(out, in, 2048) == 0,
          "and the save that was there is still exactly as it was");
    check(file_size(TMP) == -1, "no partial .tmp is left to be mistaken for a recovery");

    system("rm -rf " DIR);
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
