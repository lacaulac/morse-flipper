#include "morse_flipper_straight_trainer.h"
#include "cw.h"

#include <stdio.h>
#include <stddef.h>

static uint32_t straight_rand(MorseFlipperStraightTrainer* trainer)
{
    trainer->rng_state = trainer->rng_state * 1103515245u + 12345u;
    return trainer->rng_state;
}

static const char* straight_morse(char ch)
{
    static char code[8];
    size_t i = 0U;
    uint8_t cw_char = cw(ch);

    if(cw_char == CW_INVALID) return ".";

    FOR_EACH_CW_SYMBOL(symbol) {
        if(i + 1U >= sizeof(code)) break;
        code[i++] = symbol ? '-' : '.';
    }

    code[i] = '\0';
    return code;
}

static void straight_update_error_view(MorseFlipperStraightTrainer* trainer)
{
    size_t i;
    size_t at;
    size_t tview_at;
    int32_t diff_ms;
    uint16_t want_ms;
    uint32_t total_pct;
    uint32_t counted;
    uint8_t bars;

    if(!trainer) return;

    at = 0;
    tview_at = 0;
    total_pct = 0;
    counted = 0;
    trainer->error_bars[0] = 0;
    trainer->timing_view[0] = 0;

    for(i = 0; trainer->answer[i] && i < sizeof(trainer->target_marks_ms) / sizeof(trainer->target_marks_ms[0]); i++)
    {
        if(!trainer->answer_marks_ms[i]) continue;
        want_ms = trainer->target_marks_ms[i] ? trainer->target_marks_ms[i] : 1;
        diff_ms = (int32_t)trainer->answer_marks_ms[i] - (int32_t)want_ms;
        bars = (uint8_t)((diff_ms < 0 ? -diff_ms : diff_ms) / 10);
        if(!bars) bars = 1;
        if(at + 5u >= sizeof(trainer->error_bars)) break;

        trainer->error_bars[at++] = trainer->answer[i];
        trainer->error_bars[at++] = ':';
        if(diff_ms < -5)
        {
            while(bars-- && at + 1u < sizeof(trainer->error_bars)) trainer->error_bars[at++] = '<';
        }
        else if(diff_ms > 5)
        {
            while(bars-- && at + 1u < sizeof(trainer->error_bars)) trainer->error_bars[at++] = '>';
        }
        else
        {
            trainer->error_bars[at++] = '=';
        }
        trainer->error_bars[at++] = ' ';
        trainer->error_bars[at] = 0;

        tview_at += (size_t)snprintf(
            trainer->timing_view + tview_at,
            sizeof(trainer->timing_view) - tview_at,
            "%c%u/%u ",
            trainer->answer[i],
            (unsigned)trainer->answer_marks_ms[i],
            (unsigned)want_ms);
        total_pct += (uint32_t)((diff_ms < 0 ? -diff_ms : diff_ms) * 100 / want_ms);
        counted++;
        if(tview_at >= sizeof(trainer->timing_view)) break;
    }

    if(at > 0 && at < sizeof(trainer->error_bars))
    {
        trainer->error_bars[at - 1] = 0;
    }
    if(tview_at > 0 && tview_at < sizeof(trainer->timing_view))
    {
        trainer->timing_view[tview_at - 1] = 0;
    }
    trainer->average_drift_percent = counted ? (uint8_t)(total_pct / counted) : 0;
}

void morse_flipper_straight_trainer_init(MorseFlipperStraightTrainer* trainer)
{
    if(!trainer) return;
    trainer->target_char = 'E';
    trainer->target_morse[0] = '.';
    trainer->target_morse[1] = 0;
    trainer->answer[0] = 0;
    trainer->error_bars[0] = 0;
    trainer->timing_view[0] = 0;
    trainer->average_mark_error_ms = 0;
    trainer->average_drift_percent = 0;
    trainer->active = false;
    trainer->rng_state = 7;
}

void morse_flipper_straight_trainer_start(
    MorseFlipperStraightTrainer* trainer,
    const char* charset,
    uint16_t dit_ms)
{
    size_t len;
    size_t i;
    const char* morse;

    if(!trainer || !charset || !charset[0]) return;
    if(!dit_ms) dit_ms = 60;

    for(len = 0; charset[len]; len++)
    {
    }

    trainer->target_char = charset[straight_rand(trainer) % len];
    morse = straight_morse(trainer->target_char);
    for(i = 0; morse[i] && i + 1u < sizeof(trainer->target_morse); i++)
    {
        trainer->target_morse[i] = morse[i];
    }
    trainer->target_morse[i] = 0;
    trainer->answer[0] = 0;
    trainer->error_bars[0] = 0;
    trainer->timing_view[0] = 0;
    trainer->average_mark_error_ms = 0;
    trainer->average_drift_percent = 0;
    for(i = 0; i < sizeof(trainer->target_marks_ms) / sizeof(trainer->target_marks_ms[0]); i++)
    {
        trainer->target_marks_ms[i] = 0;
    }
    for(i = 0; i < sizeof(trainer->answer_marks_ms) / sizeof(trainer->answer_marks_ms[0]); i++)
    {
        trainer->answer_marks_ms[i] = 0;
    }
    for(i = 0; trainer->target_morse[i] && i < sizeof(trainer->target_marks_ms) / sizeof(trainer->target_marks_ms[0]); i++)
    {
        trainer->target_marks_ms[i] = trainer->target_morse[i] == '-' ? (uint16_t)(dit_ms * 3u) : dit_ms;
    }
    trainer->active = true;
}

void morse_flipper_straight_trainer_feed(
    MorseFlipperStraightTrainer* trainer,
    char elem,
    uint16_t mark_ms)
{
    size_t len;
    size_t i;
    uint32_t total_error;
    uint32_t counted;
    uint16_t want_ms;

    if(!trainer || !trainer->active) return;
    if(elem != '.' && elem != '-') return;

    for(len = 0; trainer->answer[len]; len++)
    {
    }
    if(len + 1u >= sizeof(trainer->answer)) return;
    trainer->answer[len] = elem;
    trainer->answer[len + 1] = 0;
    trainer->answer_marks_ms[len] = mark_ms;

    total_error = 0;
    counted = 0;
    for(i = 0; trainer->target_morse[i] && i < sizeof(trainer->target_marks_ms) / sizeof(trainer->target_marks_ms[0]); i++)
    {
        if(!trainer->answer_marks_ms[i]) continue;
        want_ms = trainer->target_marks_ms[i];
        if(trainer->answer_marks_ms[i] >= want_ms)
        {
            total_error += trainer->answer_marks_ms[i] - want_ms;
        }
        else
        {
            total_error += want_ms - trainer->answer_marks_ms[i];
        }
        counted++;
    }
    trainer->average_mark_error_ms = counted ? (uint16_t)(total_error / counted) : 0;
    straight_update_error_view(trainer);
}

char morse_flipper_straight_trainer_target_char(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->target_char : '?';
}

const char* morse_flipper_straight_trainer_target_morse(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->target_morse : "";
}

const char* morse_flipper_straight_trainer_answer(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->answer : "";
}

uint16_t morse_flipper_straight_trainer_average_mark_error_ms(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->average_mark_error_ms : 0;
}

uint8_t morse_flipper_straight_trainer_average_drift_percent(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->average_drift_percent : 0;
}

const char* morse_flipper_straight_trainer_error_bars(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->error_bars : "";
}

const char* morse_flipper_straight_trainer_timing_view(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->timing_view : "";
}

bool morse_flipper_straight_trainer_active(const MorseFlipperStraightTrainer* trainer)
{
    return trainer ? trainer->active : false;
}
