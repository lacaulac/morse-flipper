#ifndef yo3gnd_straight_trainer_2234
#define yo3gnd_straight_trainer_2234

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    char target_char;
    char target_morse[8];
    char answer[16];
    char error_bars[48];
    char timing_view[96];
    uint16_t target_marks_ms[8];
    uint16_t answer_marks_ms[16];
    uint16_t average_mark_error_ms;
    uint8_t average_drift_percent;
    bool active;
    uint32_t rng_state;
} MorseFlipperStraightTrainer;

void morse_flipper_straight_trainer_init(MorseFlipperStraightTrainer* trainer);
void morse_flipper_straight_trainer_start(
    MorseFlipperStraightTrainer* trainer,
    const char* charset,
    uint16_t dit_ms);
void morse_flipper_straight_trainer_feed(
    MorseFlipperStraightTrainer* trainer,
    char elem,
    uint16_t mark_ms);
char morse_flipper_straight_trainer_target_char(const MorseFlipperStraightTrainer* trainer);
const char* morse_flipper_straight_trainer_target_morse(const MorseFlipperStraightTrainer* trainer);
const char* morse_flipper_straight_trainer_answer(const MorseFlipperStraightTrainer* trainer);
uint16_t morse_flipper_straight_trainer_average_mark_error_ms(const MorseFlipperStraightTrainer* trainer);
uint8_t morse_flipper_straight_trainer_average_drift_percent(const MorseFlipperStraightTrainer* trainer);
const char* morse_flipper_straight_trainer_error_bars(const MorseFlipperStraightTrainer* trainer);
const char* morse_flipper_straight_trainer_timing_view(const MorseFlipperStraightTrainer* trainer);
bool morse_flipper_straight_trainer_active(const MorseFlipperStraightTrainer* trainer);

#endif
