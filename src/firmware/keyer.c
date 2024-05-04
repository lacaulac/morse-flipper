#include "keyer.h"

void morse_keyer_init(MorseKeyer* keyer) {
    keyer->mode = MorseKeyerModeStraight;
    keyer->dit_down = false;
    keyer->dah_down = false;
    keyer->tone_on = false;
}

void morse_keyer_reset(MorseKeyer* keyer) {
    keyer->dit_down = false;
    keyer->dah_down = false;
    keyer->tone_on = false;
}

void morse_keyer_set_mode(MorseKeyer* keyer, uint8_t mode) {
    if(mode < MorseKeyerModeStraight || mode > MorseKeyerModeKeyahead) {
        mode = MorseKeyerModeStraight;
    }

    keyer->mode = mode;
}

void morse_keyer_set_paddles(MorseKeyer* keyer, bool dit_down, bool dah_down) {
    keyer->dit_down = dit_down;
    keyer->dah_down = dah_down;
}

void morse_keyer_tick(MorseKeyer* keyer) {
    switch(keyer->mode) {
    case MorseKeyerModeStraight:
    default:
        keyer->tone_on = keyer->dit_down || keyer->dah_down;
        break;
    }
}

bool morse_keyer_tone(const MorseKeyer* keyer) {
    return keyer->tone_on;
}
