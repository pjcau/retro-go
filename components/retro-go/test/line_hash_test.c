/* rg_line_hash on the PC: determinism, alignment independence, sensitivity
 * (every single-bit change of a 304-byte or 608-byte line changes the hash,
 * a palette change changes the seeded hash), and the collision rate on random
 * pairs. Build and run: test/run_line_hash_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../rg_line_hash.h"

static uint32_t rnd(void) { static uint32_t s = 0x12345678; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

int main(void)
{
    enum { LEN8 = 304, LEN16 = 608, TRIALS = 2000 };
    uint8_t buf[LEN16 + 8], copy[LEN16 + 8];
    int fail = 0, i, t;
    size_t len;

    for (len = LEN8; len <= LEN16; len += LEN16 - LEN8)
    {
        /* determinism and alignment: the same bytes at offsets 0..3 hash alike */
        for (t = 0; t < 100; t++)
        {
            uint32_t h0;
            for (i = 0; i < (int)len + 4; i++) buf[i] = rnd();
            h0 = rg_line_hash(buf, len, 0);
            for (int off = 1; off < 4; off++)
            {
                memmove(copy + off, buf, len);
                if (rg_line_hash(copy + off, len, 0) != h0) { printf("FAIL alignment off %d len %zu\n", off, len); fail++; }
            }
            if (rg_line_hash(buf, len, 0) != h0) { printf("FAIL determinism\n"); fail++; }
        }
        /* sensitivity: one bit flipped anywhere -> a different hash, every time */
        for (t = 0; t < TRIALS; t++)
        {
            uint32_t h0, h1;
            int bit = rnd() % (len * 8);
            for (i = 0; i < (int)len; i++) buf[i] = rnd();
            h0 = rg_line_hash(buf, len, 7);
            buf[bit / 8] ^= 1 << (bit % 8);
            h1 = rg_line_hash(buf, len, 7);
            if (h0 == h1) { printf("FAIL one-bit change not seen at bit %d len %zu\n", bit, len); fail++; }
        }
        /* a byte changed and another byte changed back elsewhere (two-word change) */
        for (t = 0; t < TRIALS; t++)
        {
            uint32_t h0;
            for (i = 0; i < (int)len; i++) buf[i] = rnd();
            h0 = rg_line_hash(buf, len, 7);
            buf[rnd() % len] ^= 1 + rnd() % 255;
            buf[rnd() % len] ^= 1 + rnd() % 255;
            if (rg_line_hash(buf, len, 7) == h0) { printf("FAIL two-byte change not seen len %zu\n", len); fail++; }
        }
    }
    /* the seed (the palette hash) separates identical lines under different palettes */
    {
        uint16_t pal[256], pal2[256];
        for (i = 0; i < 256; i++) pal[i] = pal2[i] = rnd();
        pal2[rnd() % 256] ^= 0x0020;
        for (i = 0; i < LEN8; i++) buf[i] = rnd();
        if (rg_line_hash(buf, LEN8, rg_line_hash(pal, sizeof pal, 0)) == rg_line_hash(buf, LEN8, rg_line_hash(pal2, sizeof pal2, 0)))
        { printf("FAIL palette change not seen\n"); fail++; }
    }
    /* collisions between random lines: expect none in 1e6 pairs (2^-32 each) */
    {
        int coll = 0;
        for (t = 0; t < 1000000; t++)
        {
            uint32_t a, b;
            for (i = 0; i < 16; i++) buf[i] = rnd();
            a = rg_line_hash(buf, 16, 0);
            buf[rnd() % 16] ^= 1 + rnd() % 255;
            b = rg_line_hash(buf, 16, 0);
            coll += a == b;
        }
        printf("random one-byte changes on 16-byte lines: %d collisions in 1e6\n", coll);
        if (coll) fail++;
    }
    printf(fail ? "line_hash_test: %d FAILURES\n" : "line_hash_test: PASS\n", fail);
    return fail != 0;
}
