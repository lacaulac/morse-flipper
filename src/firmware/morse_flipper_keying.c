static const MorseFlipperTone* morse_flipper_current_tone(const MorseFlipperApp* app)
{
    if(app == NULL) return &morse_flipper_tones[0];

    if(app->vail_tone_active && app->vail_tone_idx < COUNT_OF(morse_flipper_tones))
        return &morse_flipper_tones[app->vail_tone_idx];

    if(app->tone_idx >= COUNT_OF(morse_flipper_tones))
        return &morse_flipper_tones[0];

    return &morse_flipper_tones[app->tone_idx];
}

static const char* morse_flipper_tone_name(const MorseFlipperApp* app)
{
    if(app != NULL && app->tone_idx == MORSE_FLIPPER_TONE_OFF_IDX) return "Off";
    return morse_flipper_current_tone(app)->name;
}

static float morse_flipper_active_tone_hz(const MorseFlipperApp* app)
{
    float hz = morse_flipper_current_tone(app)->hz;

    if(app != NULL && app->vail_tone_active && app->vail_tone_idx < COUNT_OF(morse_flipper_tones))
        hz = morse_flipper_tones[app->vail_tone_idx].hz;

    return hz;
}

static bool morse_flipper_buzz_ok(const MorseFlipperApp* app)
{
    if(app == NULL) return false;
    return app->tone_idx != MORSE_FLIPPER_TONE_OFF_IDX;
}

static bool morse_flipper_use_pwm_buzzer(const MorseFlipperApp* app)
{
    if(app == NULL || app->screen != MorseFlipperScreenRun) return false;
    return morse_flipper_buzz_ok(app) && app->audio_pwm.running;
}

static bool morse_flipper_any_active_notes(const MorseFlipperApp* app)
{
    if(app == NULL) return false;
    return app->note_src[0] != 0U || app->note_src[1] != 0U || app->note_src[2] != 0U;
}

static void morse_flipper_tone_stop(MorseFlipperApp* app)
{
    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }

    app->tone_on = false;
    app->sp_owned = false;
    app->sp_busy = false;
    app->sp_hz = 0.0f;
}

static void morse_flipper_tone_start(MorseFlipperApp* app)
{
    float hz;

    if(app->tone_on) return;

    if(!app->sp_owned) {
        if(furi_hal_speaker_acquire(30U)) {
            app->sp_owned = true;
        } else {
            app->sp_busy = true;
            return;
        }
    }

    if(!furi_hal_speaker_is_mine()) {
        app->tone_on = false;
        app->sp_owned = false;
        app->sp_busy = true;
        return;
    }

    hz = morse_flipper_active_tone_hz(app);
    furi_hal_speaker_start(hz, MORSE_FLIPPER_VOLUME);
    app->tone_on = true;
    app->sp_hz = hz;
    app->sp_busy = false;
}

static void morse_flipper_update_sidetone(MorseFlipperApp* app)
{
    bool want_tx_tone = morse_flipper_any_active_notes(app) || (app->prev_n > 0U);
    bool want_aux_tone = app->trainer_playback_mark || app->sk_play_mark ||
                         app->session_result_tone;
    bool want_tone = want_aux_tone ||
                     (want_tx_tone && !morse_flipper_use_pwm_buzzer(app) &&
                      morse_flipper_buzz_ok(app));

    if(morse_flipper_use_pwm_buzzer(app)) {
        if(app->sp_owned || app->tone_on) {
            morse_flipper_tone_stop(app);
        }

        morse_flipper_audio_pwm_set_gate(&app->audio_pwm, want_tx_tone);
        if(!want_aux_tone) {
            app->tone_on =
                want_tx_tone || morse_flipper_audio_pwm_on(&app->audio_pwm);
            app->sp_busy = false;
            return;
        }
    }

    if(want_tone) {
        float hz = morse_flipper_active_tone_hz(app);

        morse_flipper_tone_start(app);
        if(app->tone_on && app->sp_owned && furi_hal_speaker_is_mine() &&
           app->sp_hz != hz) {
            furi_hal_speaker_start(hz, MORSE_FLIPPER_VOLUME);
            app->sp_hz = hz;
            app->sp_busy = false;
        }
    } else {
        morse_flipper_tone_stop(app);
    }
}

static uint32_t morse_flipper_note_source_for_paddle(uint8_t paddle)
{
    return (paddle == MorseKeyerPaddleDit) ? MORSE_SOURCE_KEYER_DIT : MORSE_SOURCE_KEYER_DAH;
}

static uint8_t morse_flipper_note_for_paddle(uint8_t paddle)
{
    return (paddle == MorseKeyerPaddleDit) ? 1U : 2U;
}

static void morse_flipper_set_note_source( MorseFlipperApp* app, uint8_t note, uint32_t source_mask, bool active)
{
    uint32_t now_ms;

    if(note >= COUNT_OF(app->note_src)) return;

    uint32_t before = app->note_src[note];
    uint32_t after = active ? (before | source_mask) : (before & ~source_mask);

    if(before == after) return;

    app->note_src[note] = after;
    morse_flipper_update_sidetone(app);
    if(app->screen == MorseFlipperScreenRf && app->rf_live) {
        now_ms = furi_get_tick();
        app->rf_tail_at =
            now_ms + ((uint32_t)morse_flipper_current_dit_ms(app) * MORSE_FLIPPER_RF_TX_TAIL_DITS);

        if(morse_flipper_any_active_notes(app) && !app->radio.tx_on) {
            morse_flipper_radio_sync_live( &app->radio, morse_flipper_rf_frequency_hz(&app->rf), true, true);
        }
        morse_flipper_radio_set_tx_level(&app->radio, morse_flipper_any_active_notes(app));
    }

    if(before == 0U && after != 0U) {
        morse_flipper_send_transport_note(app, note, true);
    } else if(before != 0U && after == 0U) {
        morse_flipper_send_transport_note(app, note, false);
    }
}

static void morse_flipper_release_all_notes(MorseFlipperApp* app)
{
    uint32_t note_src[COUNT_OF(app->note_src)];

    for(size_t note = 0; note < COUNT_OF(app->note_src); note++) {
        note_src[note] = app->note_src[note];
        app->note_src[note] = 0U;
    }

    morse_flipper_update_sidetone(app);

    for(size_t note = 0; note < COUNT_OF(app->note_src); note++) {
        if(note_src[note] != 0U) morse_flipper_send_transport_note(app, (uint8_t)note, false);
    }

    if(app->screen == MorseFlipperScreenRf && app->rf_live) {
        uint32_t now_ms = furi_get_tick();
        app->rf_tail_at =
            now_ms + ((uint32_t)morse_flipper_current_dit_ms(app) * MORSE_FLIPPER_RF_TX_TAIL_DITS);
        morse_flipper_radio_set_tx_level(&app->radio, false);
    }
}

static void morse_flipper_drain_keyer_events(MorseFlipperApp* app)
{
    MorseKeyerEvent event;

    while(morse_keyer_pop_event(&app->keyer, &event)) {
        if((app->screen == MorseFlipperScreenTrainer ||
            app->screen == MorseFlipperScreenSession) &&
           event.type == MorseKeyerEventPress &&
           morse_trainer_phase(&app->trainer) == MorseTrainerPhaseRepeat) {
            morse_trainer_feed_element( &app->trainer, event.paddle == MorseKeyerPaddleDit ? '.' : '-');
        }

        if(app->screen == MorseFlipperScreenSession &&
           morse_trainer_phase(&app->trainer) == MorseTrainerPhaseRepeat)
            app->session_last_input_at = furi_get_tick();

        morse_flipper_set_note_source(
            app,
            morse_flipper_note_for_paddle(event.paddle),
            morse_flipper_note_source_for_paddle(event.paddle),
            event.type == MorseKeyerEventPress);
    }
}

static void morse_flipper_set_paddle_source( MorseFlipperApp* app, uint8_t paddle, uint32_t source_mask, bool active, uint32_t now_ms)
{
    if(paddle >= MorseKeyerPaddleCount) return;

    uint32_t before = app->paddle_sources[paddle];
    uint32_t after = active ? (before | source_mask) : (before & ~source_mask);

    if(before == after) return;

    app->paddle_sources[paddle] = after;
    morse_keyer_paddle_event(&app->keyer, paddle, after != 0U, now_ms);
    morse_keyer_tick(&app->keyer, now_ms);
    morse_flipper_drain_keyer_events(app);
}

static void morse_flipper_refresh_keyer(MorseFlipperApp* app, uint32_t now_ms)
{
    morse_keyer_reset(&app->keyer);
    morse_flipper_drain_keyer_events(app);
    morse_keyer_set_mode(&app->keyer, morse_flipper_current_keyer_mode(app));
    morse_keyer_set_dit_duration(&app->keyer, morse_flipper_current_dit_ms(app));

    for(uint8_t paddle = 0; paddle < MorseKeyerPaddleCount; paddle++) {
        if(app->paddle_sources[paddle] != 0U) morse_keyer_paddle_event(&app->keyer, paddle, true, now_ms);
    }

    morse_keyer_tick(&app->keyer, now_ms);
    morse_flipper_drain_keyer_events(app);
}

static void morse_flipper_btn_pad_clear(MorseFlipperApp* app, uint32_t now_ms)
{
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDit, MORSE_PADDLE_SOURCE_BTN_OK, false, now_ms);
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDah, MORSE_PADDLE_SOURCE_BTN_OK, false, now_ms);
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDit, MORSE_PADDLE_SOURCE_BTN_BACK, false, now_ms);
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDah, MORSE_PADDLE_SOURCE_BTN_BACK, false, now_ms);
}

static void morse_flipper_resync_button_paddles(MorseFlipperApp* app, uint32_t now_ms)
{
    morse_flipper_btn_pad_clear(app, now_ms);

    if(app->ok_down) {
        morse_flipper_set_paddle_source( app, morse_flipper_button_ok_paddle(app), MORSE_PADDLE_SOURCE_BTN_OK, true, now_ms);
    }

    if(app->back_down) {
        morse_flipper_set_paddle_source(
            app,
            morse_flipper_button_back_paddle(app),
            MORSE_PADDLE_SOURCE_BTN_BACK,
            true,
            now_ms);
    }
}

static void morse_flipper_btn_clear(MorseFlipperApp* app, uint32_t now_ms)
{
    app->left_down = false;
    app->ok_down = false;
    app->back_down = false;
    morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_BTN, false);
    morse_flipper_btn_pad_clear(app, now_ms);
}
