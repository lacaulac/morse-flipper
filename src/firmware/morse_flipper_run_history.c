#include "morse_flipper_run_history.h"

#include <string.h>

static void morse_flipper_run_history_next_row(MorseFlipperRunHistory* history)
{
    if(history == NULL) return;

    if(history->used_rows < MORSE_FLIPPER_RUN_HISTORY_ROWS) {
        history->current_row = history->used_rows++;
        history->line[history->current_row][0] = '\0';
        return;
    }

    memcpy(history->line[0], history->line[1], MORSE_FLIPPER_RUN_HISTORY_COLS);
    memcpy(history->line[1], history->line[2], MORSE_FLIPPER_RUN_HISTORY_COLS);
    history->current_row = MORSE_FLIPPER_RUN_HISTORY_ROWS - 1U;
    history->line[history->current_row][0] = '\0';
}

static void morse_flipper_run_history_append_ch(MorseFlipperRunHistory* history, char ch)
{
    char* line;
    size_t len;

    if(history == NULL || ch == '\0') return;
    if(ch == '|') ch = ' ';

    line = history->line[history->current_row];
    len = strlen(line);

    if(ch == ' ') {
        if(len == 0U || line[len - 1U] == ' ') return;
    }

    if(len + 1U >= MORSE_FLIPPER_RUN_HISTORY_COLS) {
        morse_flipper_run_history_next_row(history);
        line = history->line[history->current_row];
        len = 0U;
        if(ch == ' ') return;
    }

    line[len] = ch;
    line[len + 1U] = '\0';
}

void morse_flipper_run_history_reset(MorseFlipperRunHistory* history)
{
    if(history == NULL) return;

    memset(history, 0, sizeof(*history));
    history->used_rows = 1U;
}

void morse_flipper_run_history_append(MorseFlipperRunHistory* history, const char* text)
{
    if(history == NULL || text == NULL) return;

    if(history->used_rows == 0U) morse_flipper_run_history_reset(history);

    while(*text) {
        morse_flipper_run_history_append_ch(history, *text++);
    }
}
