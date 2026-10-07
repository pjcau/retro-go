/*
    Commodore 64 core on retro-go: the stub that replaces MCUME's (Teensy's)
    IntervalTimer.

    On the Teensy the VIC was driven by a hardware timer firing once per
    raster line, and it trimmed that timer's period every frame to absorb the
    time reSID had stolen. On retro-go the front end paces whole frames
    (rg_system_tick / rg_display_sync), so the period the VIC computes is
    simply thrown away -- the member stays so vic.cpp needs no edit there.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_TIMER_H_
#define C64_TIMER_H_

#ifdef __cplusplus

class MyIntervalTimer
{
public:
    void setIntervalFast(float microseconds) { interval = microseconds; }
    void end(void) {}
    void priority(unsigned char n) { (void)n; }
    float interval;
};

#endif

#endif
