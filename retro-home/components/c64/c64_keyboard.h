/*
    Commodore 64 core on retro-go: what is left of MCUME's keyboard.h.

    The USB-host / PS2 / I2C keyboards are gone; CIA1's two port handlers
    remain, and they are implemented in c64_machine.cpp against the key state
    the front end sets through c64.h.

    GPL v3 or later -- see LICENSE.md.
*/
#ifndef C64_KEYBOARD_H_
#define C64_KEYBOARD_H_

#include <stdint.h>

uint8_t cia1PORTA(void);
uint8_t cia1PORTB(void);

#endif
