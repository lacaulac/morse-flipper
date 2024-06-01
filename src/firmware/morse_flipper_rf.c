#include "morse_flipper_rf.h"
#include "morse_flipper_paths.h"

#ifdef MORSE_FLIPPER_FAP
#include <furi.h>
#include <storage/storage.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    RfStepHz = 50000,
    RfBandGapHz = 20000000,
};

static const MorseFlipperRfBand fallback_bands[] = {
    {300000000u, 348000000u},
    {387000000u, 464000000u},
    {779000000u, 928000000u},
};

static void rf_push_tx_log(MorseFlipperRf* rf, const char* line)
{
    size_t slot;

    if(!rf || !line) return;

    if(rf->tx_log_count < sizeof(rf->tx_log) / sizeof(rf->tx_log[0]))
    {
        slot = (rf->tx_log_start + rf->tx_log_count) % (sizeof(rf->tx_log) / sizeof(rf->tx_log[0]));
        rf->tx_log_count++;
    }
    else
    {
        slot = rf->tx_log_start;
        rf->tx_log_start = (rf->tx_log_start + 1u) % (sizeof(rf->tx_log) / sizeof(rf->tx_log[0]));
    }

    snprintf(rf->tx_log[slot], sizeof(rf->tx_log[slot]), "%s", line);
}

static void rf_refresh_text(MorseFlipperRf* rf)
{
    if(!rf) return;
    snprintf(
        rf->frequency_text,
        sizeof(rf->frequency_text),
        "%lu.%03lu",
        (unsigned long)(rf->frequency_hz / 1000000u),
        (unsigned long)((rf->frequency_hz / 1000u) % 1000u));
    snprintf(
        rf->manual_khz_text,
        sizeof(rf->manual_khz_text),
        "%lu",
        (unsigned long)(rf->frequency_hz / 1000u));
}

static void rf_sort_u32(uint32_t* vals, size_t n)
{
    size_t i;
    size_t j;
    uint32_t tmp;

    if(!vals || n < 2) return;

    for(i = 0; i < n; i++)
    {
        for(j = i + 1; j < n; j++)
        {
            if(vals[j] >= vals[i]) continue;
            tmp = vals[i];
            vals[i] = vals[j];
            vals[j] = tmp;
        }
    }
}

static void rf_use_fallback_bands(MorseFlipperRf* rf)
{
    size_t i;

    if(!rf) return;
    rf->band_count = sizeof(fallback_bands) / sizeof(fallback_bands[0]);
    for(i = 0; i < rf->band_count; i++)
    {
        rf->bands[i] = fallback_bands[i];
    }
}

static void rf_build_bands_from_allowed(MorseFlipperRf* rf)
{
    size_t i;
    size_t out;

    if(!rf) return;
    if(!rf->allowed_count)
    {
        rf_use_fallback_bands(rf);
        return;
    }

    rf_sort_u32(rf->allowed_hz, rf->allowed_count);
    out = 0;
    rf->bands[0].start_hz = rf->allowed_hz[0];
    rf->bands[0].end_hz = rf->allowed_hz[0];

    for(i = 1; i < rf->allowed_count && out + 1u < sizeof(rf->bands) / sizeof(rf->bands[0]); i++)
    {
        if(rf->allowed_hz[i] == rf->allowed_hz[i - 1]) continue;
        if(rf->allowed_hz[i] - rf->allowed_hz[i - 1] > RfBandGapHz)
        {
            out++;
            rf->bands[out].start_hz = rf->allowed_hz[i];
            rf->bands[out].end_hz = rf->allowed_hz[i];
        }
        else
        {
            rf->bands[out].end_hz = rf->allowed_hz[i];
        }
    }

    rf->band_count = out + 1u;
}

static size_t rf_find_band_index(const MorseFlipperRf* rf, uint32_t hz)
{
    size_t i;
    size_t best;
    uint32_t best_diff;
    uint32_t diff;

    if(!rf || !rf->band_count) return 0;

    for(i = 0; i < rf->band_count; i++)
    {
        if(hz >= rf->bands[i].start_hz && hz <= rf->bands[i].end_hz) return i;
    }

    best = 0;
    best_diff = 0xffffffffu;
    for(i = 0; i < rf->band_count; i++)
    {
        if(hz < rf->bands[i].start_hz) diff = rf->bands[i].start_hz - hz;
        else diff = hz - rf->bands[i].end_hz;
        if(diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }
    }

    return best;
}

static uint32_t rf_clamp_to_bands(const MorseFlipperRf* rf, uint32_t hz)
{
    size_t idx;

    if(!rf || !rf->band_count) return hz;

    idx = rf_find_band_index(rf, hz);
    if(hz < rf->bands[idx].start_hz) return rf->bands[idx].start_hz;
    if(hz > rf->bands[idx].end_hz) return rf->bands[idx].end_hz;
    return hz;
}

static bool rf_manual_allowed(const MorseFlipperRf* rf, uint32_t hz)
{
    size_t i;

    if(!rf) return false;

    if(rf->allowed_count)
    {
        for(i = 0; i < rf->allowed_count; i++)
        {
            if(rf->allowed_hz[i] == hz) return true;
        }
        return false;
    }

    for(i = 0; i < rf->band_count; i++)
    {
        if(hz >= rf->bands[i].start_hz && hz <= rf->bands[i].end_hz) return true;
    }

    return false;
}

static bool rf_read_text(const char* path, char* out, size_t out_sz)
{
    if(!path || !out || out_sz < 2u) return false;
    out[0] = 0;

#ifdef MORSE_FLIPPER_FAP
    {
        bool ok = false;
        size_t got = 0;
        Storage* st = furi_record_open(RECORD_STORAGE);
        File* f = storage_file_alloc(st);

        if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING))
        {
            got = storage_file_read(f, out, out_sz - 1u);
            out[got] = 0;
            ok = true;
        }

        storage_file_close(f);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
        return ok;
    }
#else
    {
        FILE* fp = fopen(path, "rb");
        size_t got;

        if(!fp) return false;
        got = fread(out, 1, out_sz - 1u, fp);
        out[got] = 0;
        fclose(fp);
        return got != 0u;
    }
#endif
}

void morse_flipper_rf_init(MorseFlipperRf* rf)
{
    if(!rf) return;
    memset(rf, 0, sizeof(*rf));
    rf_use_fallback_bands(rf);
    morse_flipper_rf_timing_init(&rf->rx_timing);
    rf->frequency_hz = 433920000u;
    rf_refresh_text(rf);
}

void morse_flipper_rf_load_settings(MorseFlipperRf* rf, const char* path)
{
    char buf[2048];
    char* line;
    char* next;
    char digits[16];
    size_t i;
    size_t at;
    unsigned long value;

    if(!rf) return;
    rf->allowed_count = 0;

    if(path && rf_read_text(path, buf, sizeof(buf)))
    {
        line = buf;
        while(line && *line)
        {
            next = strchr(line, '\n');
            if(next) *next++ = 0;
            at = 0;
            for(i = 0; line[i] && at + 1u < sizeof(digits); i++)
            {
                if(!isdigit((unsigned char)line[i])) continue;
                while(line[i] && isdigit((unsigned char)line[i]) && at + 1u < sizeof(digits))
                {
                    digits[at++] = line[i++];
                }
                break;
            }
            digits[at] = 0;
            if(at >= 6)
            {
                value = strtoul(digits, NULL, 10);
                if(value >= 100000000u && value <= 1000000000u &&
                    rf->allowed_count < sizeof(rf->allowed_hz) / sizeof(rf->allowed_hz[0]))
                {
                    rf->allowed_hz[rf->allowed_count++] = (uint32_t)value;
                }
            }
            line = next;
        }
    }

    rf_build_bands_from_allowed(rf);
    rf->frequency_hz = rf_clamp_to_bands(rf, rf->frequency_hz);
    rf_refresh_text(rf);
}

void morse_flipper_rf_set_frequency_hz(MorseFlipperRf* rf, uint32_t hz)
{
    if(!rf) return;
    rf->frequency_hz = rf_clamp_to_bands(rf, hz);
    rf_refresh_text(rf);
}

void morse_flipper_rf_step(MorseFlipperRf* rf, int dir)
{
    size_t idx;
    int64_t next_hz;

    if(!rf || !rf->band_count || !dir) return;
    idx = rf_find_band_index(rf, rf->frequency_hz);
    next_hz = (int64_t)rf->frequency_hz + (int64_t)dir * RfStepHz;
    if(next_hz < (int64_t)rf->bands[idx].start_hz) next_hz = rf->bands[idx].start_hz;
    if(next_hz > (int64_t)rf->bands[idx].end_hz) next_hz = rf->bands[idx].end_hz;
    rf->frequency_hz = (uint32_t)next_hz;
    rf->last_error[0] = 0;
    rf_refresh_text(rf);
}

void morse_flipper_rf_next_band(MorseFlipperRf* rf)
{
    size_t idx;

    if(!rf || !rf->band_count) return;
    idx = (rf_find_band_index(rf, rf->frequency_hz) + 1u) % rf->band_count;
    rf->frequency_hz = rf->bands[idx].start_hz;
    rf->last_error[0] = 0;
    rf_refresh_text(rf);
}

bool morse_flipper_rf_set_manual_khz(MorseFlipperRf* rf, const char* text)
{
    size_t len;
    size_t i;
    unsigned long khz;
    uint32_t hz;

    if(!rf || !text) return false;

    for(len = 0; text[len]; len++)
    {
    }

    if(len != 6)
    {
        snprintf(rf->last_error, sizeof(rf->last_error), "need 6 digits");
        return false;
    }

    for(i = 0; i < len; i++)
    {
        if(!isdigit((unsigned char)text[i]))
        {
            snprintf(rf->last_error, sizeof(rf->last_error), "digits only");
            return false;
        }
    }

    khz = strtoul(text, NULL, 10);
    hz = (uint32_t)(khz * 1000u);
    if(!rf_manual_allowed(rf, hz))
    {
        snprintf(rf->last_error, sizeof(rf->last_error), "freq not allowed");
        return false;
    }

    rf->frequency_hz = hz;
    rf->last_error[0] = 0;
    rf_refresh_text(rf);
    return true;
}

void morse_flipper_rf_reset_live(MorseFlipperRf* rf)
{
    if(!rf) return;
    rf->tx_active = false;
    rf->tx_edges = 0;
    rf->tx_log_start = 0;
    rf->tx_log_count = 0;
    morse_flipper_rf_timing_init(&rf->rx_timing);
}

void morse_flipper_rf_handle_tx(MorseFlipperRf* rf, bool active, char symbol)
{
    char line[48];

    if(!rf) return;
    if(rf->tx_active == active) return;

    rf->tx_active = active;
    rf->tx_edges++;
    snprintf(
        line,
        sizeof(line),
        "ook %s %c @ %s",
        active ? "on" : "off",
        symbol ? symbol : '?',
        rf->frequency_text);
    rf_push_tx_log(rf, line);
}

void morse_flipper_rf_capture_rx_timing(MorseFlipperRf* rf, bool mark, uint16_t duration_ms)
{
    if(!rf || !duration_ms) return;
    morse_flipper_rf_timing_capture(&rf->rx_timing, mark, duration_ms, rf->frequency_text);
}

uint32_t morse_flipper_rf_frequency_hz(const MorseFlipperRf* rf)
{
    return rf ? rf->frequency_hz : 0;
}

const char* morse_flipper_rf_frequency_text(const MorseFlipperRf* rf)
{
    return rf ? rf->frequency_text : "";
}

const char* morse_flipper_rf_manual_khz_text(const MorseFlipperRf* rf)
{
    return rf ? rf->manual_khz_text : "";
}

const char* morse_flipper_rf_last_error(const MorseFlipperRf* rf)
{
    return rf ? rf->last_error : "";
}

size_t morse_flipper_rf_band_count(const MorseFlipperRf* rf)
{
    return rf ? rf->band_count : 0;
}

size_t morse_flipper_rf_current_band_index(const MorseFlipperRf* rf)
{
    return rf ? rf_find_band_index(rf, rf->frequency_hz) : 0;
}

size_t morse_flipper_rf_tx_log_count(const MorseFlipperRf* rf)
{
    return rf ? rf->tx_log_count : 0;
}

const char* morse_flipper_rf_tx_log_line(const MorseFlipperRf* rf, size_t idx)
{
    if(!rf || idx >= rf->tx_log_count) return "";
    return rf->tx_log[(rf->tx_log_start + idx) % (sizeof(rf->tx_log) / sizeof(rf->tx_log[0]))];
}

size_t morse_flipper_rf_rx_count(const MorseFlipperRf* rf)
{
    return rf ? morse_flipper_rf_timing_count(&rf->rx_timing) : 0;
}

bool morse_flipper_rf_rx_mark(const MorseFlipperRf* rf, size_t idx)
{
    return rf ? morse_flipper_rf_timing_mark(&rf->rx_timing, idx) : false;
}

uint16_t morse_flipper_rf_rx_duration_ms(const MorseFlipperRf* rf, size_t idx)
{
    return rf ? morse_flipper_rf_timing_duration_ms(&rf->rx_timing, idx) : 0;
}

size_t morse_flipper_rf_rx_log_count(const MorseFlipperRf* rf)
{
    return rf ? morse_flipper_rf_timing_log_count(&rf->rx_timing) : 0;
}

const char* morse_flipper_rf_rx_log_line(const MorseFlipperRf* rf, size_t idx)
{
    return rf ? morse_flipper_rf_timing_log_line(&rf->rx_timing, idx) : "";
}
