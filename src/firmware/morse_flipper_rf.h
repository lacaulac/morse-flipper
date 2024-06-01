#ifndef yo3gnd_rf_2239
#define yo3gnd_rf_2239

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "morse_flipper_rf_timing.h"

typedef struct
{
    uint32_t start_hz;
    uint32_t end_hz;
} MorseFlipperRfBand;

typedef struct
{
    uint32_t frequency_hz;
    uint32_t allowed_hz[256];
    size_t allowed_count;
    MorseFlipperRfBand bands[16];
    size_t band_count;
    bool tx_active;
    uint32_t tx_edges;
    char tx_log[8][48];
    size_t tx_log_start;
    size_t tx_log_count;
    MorseFlipperRfTiming rx_timing;
    char frequency_text[16];
    char manual_khz_text[16];
    char last_error[96];
} MorseFlipperRf;

void morse_flipper_rf_init(MorseFlipperRf* rf);
void morse_flipper_rf_load_settings(MorseFlipperRf* rf, const char* path);
void morse_flipper_rf_set_frequency_hz(MorseFlipperRf* rf, uint32_t hz);
void morse_flipper_rf_step(MorseFlipperRf* rf, int dir);
void morse_flipper_rf_next_band(MorseFlipperRf* rf);
bool morse_flipper_rf_set_manual_khz(MorseFlipperRf* rf, const char* text);
void morse_flipper_rf_reset_live(MorseFlipperRf* rf);
void morse_flipper_rf_handle_tx(MorseFlipperRf* rf, bool active, char symbol);
void morse_flipper_rf_capture_rx_timing(MorseFlipperRf* rf, bool mark, uint16_t duration_ms);
uint32_t morse_flipper_rf_frequency_hz(const MorseFlipperRf* rf);
const char* morse_flipper_rf_frequency_text(const MorseFlipperRf* rf);
const char* morse_flipper_rf_manual_khz_text(const MorseFlipperRf* rf);
const char* morse_flipper_rf_last_error(const MorseFlipperRf* rf);
size_t morse_flipper_rf_band_count(const MorseFlipperRf* rf);
size_t morse_flipper_rf_current_band_index(const MorseFlipperRf* rf);
size_t morse_flipper_rf_tx_log_count(const MorseFlipperRf* rf);
const char* morse_flipper_rf_tx_log_line(const MorseFlipperRf* rf, size_t idx);
size_t morse_flipper_rf_rx_count(const MorseFlipperRf* rf);
bool morse_flipper_rf_rx_mark(const MorseFlipperRf* rf, size_t idx);
uint16_t morse_flipper_rf_rx_duration_ms(const MorseFlipperRf* rf, size_t idx);
size_t morse_flipper_rf_rx_log_count(const MorseFlipperRf* rf);
const char* morse_flipper_rf_rx_log_line(const MorseFlipperRf* rf, size_t idx);

#endif
