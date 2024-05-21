#include "trainer.h"

#include <string.h>

static const char morse_trainer_koch_order[] = "KMURESNAPTLWI.JZ=FOY,VG5/Q92H38B?47C1D60X";

void morse_trainer_init(MorseTrainer* trainer) {
    if(trainer == NULL) {
        return;
    }

    memset(trainer, 0, sizeof(*trainer));
    trainer->lesson = 1U;
    trainer->group_size = 5U;
    trainer->session_groups = 10U;
    trainer->local_dit_ms = 100U;
    trainer->rng_state = 1U;
}

size_t morse_trainer_lesson_count(void) {
    return sizeof(morse_trainer_koch_order) - 1U;
}

void morse_trainer_set_lesson(MorseTrainer* trainer, uint8_t lesson) {
    uint8_t max_lesson = (uint8_t)morse_trainer_lesson_count();

    if(trainer == NULL) {
        return;
    }

    if(lesson < 1U) {
        lesson = 1U;
    }
    if(lesson > max_lesson) {
        lesson = max_lesson;
    }

    trainer->lesson = lesson;
}

uint8_t morse_trainer_lesson(const MorseTrainer* trainer) {
    if(trainer == NULL || trainer->lesson == 0U) {
        return 1U;
    }

    return trainer->lesson;
}

const char* morse_trainer_charset(const MorseTrainer* trainer) {
    static char buf[MORSE_TRAINER_CHARSET_CAP];
    size_t lesson;

    if(trainer != NULL && trainer->charset_override[0] != '\0') {
        return trainer->charset_override;
    }

    lesson = trainer ? morse_trainer_lesson(trainer) : 1U;
    if(lesson >= sizeof(buf)) {
        lesson = sizeof(buf) - 1U;
    }

    memcpy(buf, morse_trainer_koch_order, lesson);
    buf[lesson] = '\0';
    return buf;
}
