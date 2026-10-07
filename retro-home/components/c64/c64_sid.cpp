/*
    Commodore 64 core on retro-go: the SID, reSID 0.16 (Dag Lem,
    GPL v2 or later) as shipped by MCUME (MCUME_esp32/esp64/main/reSID,
    commit 27f6b906).

    reSID's sources are .cc files included from this one translation unit,
    exactly as MCUME's sid.cpp did it, so that nothing in reSID/ had to be
    renamed or edited. Only the 6581 wave tables are pulled in.

    This file replaces MCUME's reSID.cpp (class AudioPlaySID): the machine
    talks to the SID through the plain C functions of c64_sid.h.

    GPL v3 or later for the port -- see LICENSE.md.
*/

#include "c64_sid.h"

#include <string.h>

/* reSID's version.cc wants VERSION from the build system. */
#ifndef VERSION
#define VERSION "reSID 0.16 (MCUME)"
#endif

#include "reSID/envelope.cc"
#include "reSID/extfilt.cc"
#include "reSID/filter.cc"
#include "reSID/pot.cc"
#include "reSID/version.cc"
#include "reSID/voice.cc"

#include "reSID/wave6581__ST.cc"
#include "reSID/wave6581_P_T.cc"
#include "reSID/wave6581_PS_.cc"
#include "reSID/wave6581_PST.cc"

#include "reSID/wave.cc"

#include "reSID/sid.cc"

static SID sid;
static cycle_count sid_pending;
static cycle_count sid_pending_max = 1 << 16;
static float sid_cycles_per_sample = 31.0f;

void c64_sid_init(float clock_rate, float sample_rate)
{
    sid.reset();
    sid.set_sampling_parameters(clock_rate, SAMPLE_FAST, sample_rate);
    sid_cycles_per_sample = clock_rate / sample_rate;
    /* Never let more than ~4 frames of cycles pile up: if the emulator runs
       ahead of the audio sink, the backlog is dropped rather than turned into
       a growing burst of sound. */
    sid_pending_max = (cycle_count)(clock_rate / 12.0f);
    sid_pending = 0;
}

void c64_sid_reset(void)
{
    sid.reset();
    sid_pending = 0;
}

uint8_t c64_sid_read(uint8_t reg)
{
    return (uint8_t)sid.read(reg & 0x1F);
}

void c64_sid_write(uint8_t reg, uint8_t value)
{
    sid.write(reg & 0x1F, value);
}

int c64_sid_render(int16_t *buffer, int samples, int cycles)
{
    if (samples <= 0)
        return 0;

    sid_pending += cycles;
    if (sid_pending > sid_pending_max)
        sid_pending = sid_pending_max;

    int produced = 0;
    for (int guard = 0; produced < samples && guard < 16; guard++)
    {
        cycle_count delta = sid_pending;
        if (delta < 1)
        {
            /* The frame did not carry enough cycles for a full audio period
               (reSID rounds down): lend the SID what it needs and repay it
               out of the next frame's cycles. */
            delta = 1 + (cycle_count)((samples - produced) * sid_cycles_per_sample);
            sid_pending += delta;
        }
        const cycle_count before = delta;
        const int n = sid.clock(delta, (short *)(buffer + produced), samples - produced);
        sid_pending -= (before - delta);
        produced += n;
        if (n == 0 && delta == before)
            break;
    }

    /* A PAL frame is 1/50.125 s, but retro-go's tick rate is a whole 50, so
       each frame asks for about 1.6 samples more than its cycles pay for. The
       SID is lent them above; without a floor the debt would run for the whole
       session. Dropping it costs 0.25% of pitch, which is inaudible. */
    if (sid_pending < -sid_pending_max)
        sid_pending = 0;

    /* Should not happen, but never hand retro-go a partly filled buffer. */
    const int16_t last = produced > 0 ? buffer[produced - 1] : 0;
    for (int i = produced; i < samples; i++)
        buffer[i] = last;

    return samples;
}

int c64_sid_state_size(void)
{
    return (int)sizeof(SID::State);
}

void c64_sid_state_save(void *dest)
{
    const SID::State state = sid.read_state();
    memcpy(dest, &state, sizeof(state));
}

void c64_sid_state_load(const void *src)
{
    SID::State state;
    memcpy(&state, src, sizeof(state));
    sid.write_state(state);
}
