/* rg_line_hash: the "did this line change" hash of the display task (D1 of the
 * Arcade 60 fps plan). Word-wise xor-multiply, seeded: 2 operations per 4 bytes
 * against the byte-pair mixing of rg_hash(). A change in any one word always
 * changes the result (odd multiplier, xor); two changes collide with the usual
 * 2^-32 chance. Proven on the PC by test/line_hash_test.c. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RG_LINE_HASH_MUL 0x9E3779B1u

static inline uint32_t rg_line_hash(const void *data, size_t len, uint32_t seed)
{
    uint32_t h = seed ^ (uint32_t)len;
    const uint8_t *p = data;
    size_t n = len >> 2;
    if (((uintptr_t)p & 3) == 0)
    {
        const uint32_t *w = (const uint32_t *)p;
        for (; n; n--)
            h = (h ^ *w++) * RG_LINE_HASH_MUL;
        p = (const uint8_t *)w;
    }
    else if (((uintptr_t)p & 1) == 0)
    {
        const uint16_t *w = (const uint16_t *)p;
        for (; n; n--, w += 2)
            h = (h ^ (w[0] | ((uint32_t)w[1] << 16))) * RG_LINE_HASH_MUL;
        p = (const uint8_t *)w;
    }
    else
    {
        for (; n; n--, p += 4)
        {
            uint32_t t;
            memcpy(&t, p, 4);
            h = (h ^ t) * RG_LINE_HASH_MUL;
        }
    }
    if (len & 3)
    {
        uint32_t t = 0;
        memcpy(&t, p, len & 3);
        h = (h ^ t) * RG_LINE_HASH_MUL;
    }
    return h ^ (h >> 16);
}
