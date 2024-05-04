#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MorseKeyerModeStraight = 1,
    MorseKeyerModeBug = 2,
    MorseKeyerModeElBug = 3,
    MorseKeyerModeSingleDot = 4,
    MorseKeyerModeUltimatic = 5,
    MorseKeyerModePlainIambic = 6,
    MorseKeyerModeIambicA = 7,
    MorseKeyerModeIambicB = 8,
    MorseKeyerModeKeyahead = 9,
} MorseKeyerMode;

typedef struct {
    uint8_t mode;
    bool dit_down;
    bool dah_down;
    bool tone_on;
} MorseKeyer;

void morse_keyer_init(MorseKeyer* keyer);
void morse_keyer_reset(MorseKeyer* keyer);
void morse_keyer_set_mode(MorseKeyer* keyer, uint8_t mode);
void morse_keyer_set_paddles(MorseKeyer* keyer, bool dit_down, bool dah_down);
void morse_keyer_tick(MorseKeyer* keyer);
bool morse_keyer_tone(const MorseKeyer* keyer);
