static void morse_flipper_release_mouse_buttons(void) {
    furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT);
    furi_hal_hid_mouse_release(HID_MOUSE_BTN_RIGHT);
}

static void morse_flipper_send_mouse_note(MorseFlipperApp* app, uint8_t note, bool active) {
    uint8_t btn = morse_pc_mouse_button(note, app->mouse_invert);

    if(btn == MorsePcMouseBtnNone) return;

    if(active) {
        furi_hal_hid_mouse_press(btn);
    } else {
        furi_hal_hid_mouse_release(btn);
    }
}

static void morse_flipper_send_transport_note(MorseFlipperApp* app, uint8_t note, bool active) {
    switch(app->pc_mode) {
    case MorseFlipperPcModeMidi:
        morse_flipper_send_midi_note(note, active);
        break;
    case MorseFlipperPcModeKeyboard:
        morse_flipper_send_keyboard_note(app, note, active);
        break;
    case MorseFlipperPcModeMouse:
        morse_flipper_send_mouse_note(app, note, active);
        break;
    default:
        break;
    }
}

static void morse_flipper_clear_vail_overrides(MorseFlipperApp* app) {
    bool had_tone_override = app->vail_tone_active;

    app->vail_mode_active = false;
    app->vail_speed_active = false;
    app->vail_tone_active = false;

    if(had_tone_override && app->tone_on && app->sp_owned && furi_hal_speaker_is_mine()) {
        furi_hal_speaker_start(morse_flipper_current_tone(app)->hz, MORSE_FLIPPER_VOLUME);
    }
}

static void morse_flipper_apply_vail_speed(MorseFlipperApp* app, uint8_t value) {
    uint16_t dit_ms = (value == 0U) ? 1U : ((uint16_t)value * 2U);

    if(app->vail_speed_active && app->vail_dit_ms == dit_ms) {
        return;
    }

    app->vail_speed_active = true;
    app->vail_dit_ms = dit_ms;
    morse_flipper_refresh_keyer(app, furi_get_tick());
    morse_flipper_view_dirty(app);
}

static void morse_flipper_apply_vail_mode(MorseFlipperApp* app, uint8_t program) {
    uint8_t mode = morse_keyer_mode_valid(program) ? program : MorseKeyerModePassthrough;

    if(app->vail_mode_active && app->vail_keyer_mode == mode) {
        return;
    }

    app->vail_mode_active = true;
    app->vail_keyer_mode = mode;
    morse_flipper_refresh_keyer(app, furi_get_tick());
    morse_flipper_view_dirty(app);
}

static void morse_flipper_apply_vail_tone(MorseFlipperApp* app, uint8_t midi_note) {
    uint8_t tone_idx = morse_flipper_nearest_tone_idx_for_midi(midi_note);

    if(app->vail_tone_active && app->vail_tone_idx == tone_idx) {
        return;
    }

    app->vail_tone_active = true;
    app->vail_tone_idx = tone_idx;

    if(app->tone_on && app->sp_owned && furi_hal_speaker_is_mine()) {
        furi_hal_speaker_start(morse_flipper_current_tone(app)->hz, MORSE_FLIPPER_VOLUME);
    } else {
        morse_flipper_update_sidetone(app);
    }

    morse_flipper_view_dirty(app);
}

static void morse_flipper_resync_transport_notes(MorseFlipperApp* app) {
    for(uint8_t note = 0U; note < COUNT_OF(app->note_src); note++) {
        if(app->note_src[note] != 0U) {
            morse_flipper_send_transport_note(app, note, true);
        }
    }
}

static uint8_t morse_flipper_cfg_tone_idx(uint8_t x) {
    if(x == MORSE_FLIPPER_TONE_OFF_IDX) return MORSE_FLIPPER_TONE_OFF_IDX;
    if(x < COUNT_OF(morse_flipper_tones)) return x;
    return 0U;
}

static void morse_flipper_load_config(MorseFlipperApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    MorseFlipperConfig config;
    MorseFlipperConfigV5 config_v5;
    MorseFlipperConfigV4 config_v4;
    MorseFlipperConfigV3 config_v3;
    MorseFlipperConfigV2 config_v2;
    MorseFlipperConfigV1 config_v1;
    uint16_t got = 0U;

    if(storage_file_open(file, MORSE_FLIPPER_CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        got = storage_file_read(file, &config, sizeof(config));
        if(got == sizeof(config) && config.version == MORSE_FLIPPER_CONFIG_VERSION) {
            app->tone_idx = morse_flipper_cfg_tone_idx(config.tone_idx);

            if(config.keyer_mode >= MorseKeyerModeStraight &&
               config.keyer_mode <= MorseKeyerModeKeyahead) {
                app->keyer_mode = config.keyer_mode;
            }

            if(config.handedness <= MorseFlipperHandednessSwapped) {
                app->handedness = config.handedness;
            }

            if(config.spare0 <= MorseFlipperInputSourceButtons) {
                app->in_src = config.spare0;
            }

            morse_trainer_set_lesson(&app->trainer, config.trainer_lesson);
            morse_trainer_set_group_size(&app->trainer, config.trainer_group_size);
            morse_trainer_set_session_groups(&app->trainer, config.trainer_session_groups);
            if(config.local_dit_ms != 0U) {
                app->trainer.local_dit_ms = config.local_dit_ms;
            }
            if(morse_flipper_gpio_validate(
                   config.gpio_straight_idx,
                   config.gpio_dit_idx,
                   config.gpio_dah_idx,
                   config.gpio_ground_idx) == MorseFlipperGpioRuleOk) {
                app->gpio_straight_idx = config.gpio_straight_idx;
                app->gpio_dit_idx = config.gpio_dit_idx;
                app->gpio_dah_idx = config.gpio_dah_idx;
                app->gpio_ground_idx = config.gpio_ground_idx;
            }
            if(config.trainer_custom_set_idx <= app->custom_sets.count) {
                app->trainer.custom_set_idx = config.trainer_custom_set_idx;
            }
            if(config.usb_mode <= MorseFlipperPcModeMidi) app->pc_pref = config.usb_mode;
            if(config.usb_paddle_preset < morse_pc_paddle_preset_count())
                app->pc_paddle_preset = config.usb_paddle_preset;
            if(config.usb_straight_preset < morse_pc_straight_preset_count())
                app->pc_straight_preset = config.usb_straight_preset;
            app->mouse_invert = config.usb_mouse_invert != 0U;
            app->trainer_farn_wpm = config.trainer_farn_wpm;
            app->trainer_to_s = config.trainer_to_s;
            app->trainer_gap_s = config.trainer_gap_s;
        } else if(got == sizeof(config_v5)) {
            memcpy(&config_v5, &config, sizeof(config_v5));
            if(config_v5.version == 5U) {
                app->tone_idx = morse_flipper_cfg_tone_idx(config_v5.tone_idx);

                if(config_v5.keyer_mode >= MorseKeyerModeStraight &&
                   config_v5.keyer_mode <= MorseKeyerModeKeyahead) {
                    app->keyer_mode = config_v5.keyer_mode;
                }

                if(config_v5.handedness <= MorseFlipperHandednessSwapped) {
                    app->handedness = config_v5.handedness;
                }

                if(config_v5.spare0 <= MorseFlipperInputSourceButtons) {
                    app->in_src = config_v5.spare0;
                }

                morse_trainer_set_lesson(&app->trainer, config_v5.trainer_lesson);
                morse_trainer_set_group_size(&app->trainer, config_v5.trainer_group_size);
                morse_trainer_set_session_groups(&app->trainer, config_v5.trainer_session_groups);
                if(config_v5.local_dit_ms != 0U) {
                    app->trainer.local_dit_ms = config_v5.local_dit_ms;
                }
                if(morse_flipper_gpio_validate(
                       config_v5.gpio_straight_idx,
                       config_v5.gpio_dit_idx,
                       config_v5.gpio_dah_idx,
                       config_v5.gpio_ground_idx) == MorseFlipperGpioRuleOk) {
                    app->gpio_straight_idx = config_v5.gpio_straight_idx;
                    app->gpio_dit_idx = config_v5.gpio_dit_idx;
                    app->gpio_dah_idx = config_v5.gpio_dah_idx;
                    app->gpio_ground_idx = config_v5.gpio_ground_idx;
                }
                if(config_v5.trainer_custom_set_idx <= app->custom_sets.count) {
                    app->trainer.custom_set_idx = config_v5.trainer_custom_set_idx;
                }
                if(config_v5.usb_mode <= MorseFlipperPcModeMidi) app->pc_pref = config_v5.usb_mode;
                if(config_v5.usb_paddle_preset < morse_pc_paddle_preset_count())
                    app->pc_paddle_preset = config_v5.usb_paddle_preset;
                if(config_v5.usb_straight_preset < morse_pc_straight_preset_count())
                    app->pc_straight_preset = config_v5.usb_straight_preset;
                app->mouse_invert = config_v5.usb_mouse_invert != 0U;
            }
        } else if(got == sizeof(config_v4)) {
            memcpy(&config_v4, &config, sizeof(config_v4));
            if(config_v4.version == 4U) {
                app->tone_idx = morse_flipper_cfg_tone_idx(config_v4.tone_idx);

                if(config_v4.keyer_mode >= MorseKeyerModeStraight &&
                   config_v4.keyer_mode <= MorseKeyerModeKeyahead) {
                    app->keyer_mode = config_v4.keyer_mode;
                }

                if(config_v4.handedness <= MorseFlipperHandednessSwapped) {
                    app->handedness = config_v4.handedness;
                }

                if(config_v4.spare0 <= MorseFlipperInputSourceButtons) {
                    app->in_src = config_v4.spare0;
                }

                morse_trainer_set_lesson(&app->trainer, config_v4.trainer_lesson);
                morse_trainer_set_group_size(&app->trainer, config_v4.trainer_group_size);
                morse_trainer_set_session_groups(&app->trainer, config_v4.trainer_session_groups);
                if(config_v4.local_dit_ms != 0U) {
                    app->trainer.local_dit_ms = config_v4.local_dit_ms;
                }
                if(morse_flipper_gpio_validate(
                       config_v4.gpio_straight_idx,
                       config_v4.gpio_dit_idx,
                       config_v4.gpio_dah_idx,
                       config_v4.gpio_ground_idx) == MorseFlipperGpioRuleOk) {
                    app->gpio_straight_idx = config_v4.gpio_straight_idx;
                    app->gpio_dit_idx = config_v4.gpio_dit_idx;
                    app->gpio_dah_idx = config_v4.gpio_dah_idx;
                    app->gpio_ground_idx = config_v4.gpio_ground_idx;
                }
                if(config_v4.trainer_custom_set_idx <= app->custom_sets.count) {
                    app->trainer.custom_set_idx = config_v4.trainer_custom_set_idx;
                }
            }
        } else if(got == sizeof(config_v3)) {
            memcpy(&config_v3, &config, sizeof(config_v3));
            if(config_v3.version == 3U) {
                app->tone_idx = morse_flipper_cfg_tone_idx(config_v3.tone_idx);

                if(config_v3.keyer_mode >= MorseKeyerModeStraight &&
                   config_v3.keyer_mode <= MorseKeyerModeKeyahead) {
                    app->keyer_mode = config_v3.keyer_mode;
                }

                if(config_v3.handedness <= MorseFlipperHandednessSwapped) {
                    app->handedness = config_v3.handedness;
                }

                if(config_v3.spare0 <= MorseFlipperInputSourceButtons) {
                    app->in_src = config_v3.spare0;
                }

                morse_trainer_set_lesson(&app->trainer, config_v3.trainer_lesson);
                morse_trainer_set_group_size(&app->trainer, config_v3.trainer_group_size);
                morse_trainer_set_session_groups(&app->trainer, config_v3.trainer_session_groups);
                if(config_v3.local_dit_ms != 0U) {
                    app->trainer.local_dit_ms = config_v3.local_dit_ms;
                }
                if(morse_flipper_gpio_validate(
                       config_v3.gpio_straight_idx,
                       config_v3.gpio_dit_idx,
                       config_v3.gpio_dah_idx,
                       config_v3.gpio_ground_idx) == MorseFlipperGpioRuleOk) {
                    app->gpio_straight_idx = config_v3.gpio_straight_idx;
                    app->gpio_dit_idx = config_v3.gpio_dit_idx;
                    app->gpio_dah_idx = config_v3.gpio_dah_idx;
                    app->gpio_ground_idx = config_v3.gpio_ground_idx;
                }
            }
        } else if(got == sizeof(config_v2)) {
            memcpy(&config_v2, &config, sizeof(config_v2));
            if(config_v2.version == 2U) {
                app->tone_idx = morse_flipper_cfg_tone_idx(config_v2.tone_idx);

                if(config_v2.keyer_mode >= MorseKeyerModeStraight &&
                   config_v2.keyer_mode <= MorseKeyerModeKeyahead) {
                    app->keyer_mode = config_v2.keyer_mode;
                }

                if(config_v2.handedness <= MorseFlipperHandednessSwapped) {
                    app->handedness = config_v2.handedness;
                }

                if(config_v2.spare0 <= MorseFlipperInputSourceButtons) {
                    app->in_src = config_v2.spare0;
                }

                morse_trainer_set_lesson(&app->trainer, config_v2.trainer_lesson);
                morse_trainer_set_group_size(&app->trainer, config_v2.trainer_group_size);
                morse_trainer_set_session_groups(&app->trainer, config_v2.trainer_session_groups);
                if(config_v2.local_dit_ms != 0U) {
                    app->trainer.local_dit_ms = config_v2.local_dit_ms;
                }
            }
        } else if(got == sizeof(config_v1)) {
            memcpy(&config_v1, &config, sizeof(config_v1));
            if(config_v1.version == 1U) {
                app->tone_idx = morse_flipper_cfg_tone_idx(config_v1.tone_idx);

                if(config_v1.keyer_mode >= MorseKeyerModeStraight &&
                   config_v1.keyer_mode <= MorseKeyerModeKeyahead) {
                    app->keyer_mode = config_v1.keyer_mode;
                }
            }
        }
    }

    morse_flipper_train_fix(app);

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void morse_flipper_save_config(const MorseFlipperApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    MorseFlipperConfig config = {
        .version = MORSE_FLIPPER_CONFIG_VERSION,
        .tone_idx = app->tone_idx,
        .keyer_mode = app->keyer_mode,
        .handedness = app->handedness,
        .trainer_lesson = morse_trainer_lesson(&app->trainer),
        .trainer_group_size = morse_trainer_group_size(&app->trainer),
        .trainer_session_groups = morse_trainer_session_groups(&app->trainer),
        .spare0 = app->in_src,
        .local_dit_ms = app->trainer.local_dit_ms,
        .gpio_straight_idx = app->gpio_straight_idx,
        .gpio_dit_idx = app->gpio_dit_idx,
        .gpio_dah_idx = app->gpio_dah_idx,
        .gpio_ground_idx = app->gpio_ground_idx,
        .trainer_custom_set_idx = app->trainer.custom_set_idx,
        .usb_mode = app->pc_pref,
        .usb_paddle_preset = app->pc_paddle_preset,
        .usb_straight_preset = app->pc_straight_preset,
        .usb_mouse_invert = app->mouse_invert ? 1U : 0U,
        .trainer_farn_wpm = app->trainer_farn_wpm,
        .trainer_to_s = app->trainer_to_s,
        .trainer_gap_s = app->trainer_gap_s,
    };

    if(storage_file_open(file, MORSE_FLIPPER_CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &config, sizeof(config));
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void morse_flipper_gpio_init(MorseFlipperApp* app) {
    morse_flipper_gpio_apply(app);
}

static void morse_flipper_gpio_deinit(void) {
    morse_flipper_gpio_reset_candidates();
}

static bool morse_flipper_straight_down(void) {
    return !furi_hal_gpio_read(morse_flipper_straight_pin);
}

static bool morse_flipper_dit_down(void) {
    return !furi_hal_gpio_read(morse_flipper_dit_pin);
}

static bool morse_flipper_dah_down(void) {
    return !furi_hal_gpio_read(morse_flipper_dah_pin);
}

static bool morse_flipper_logical_dit_down(const MorseFlipperApp* app) {
    if(app->handedness == MorseFlipperHandednessSwapped) {
        return morse_flipper_dah_down();
    }

    return morse_flipper_dit_down();
}

static bool morse_flipper_logical_dah_down(const MorseFlipperApp* app) {
    if(app->handedness == MorseFlipperHandednessSwapped) {
        return morse_flipper_dit_down();
    }

    return morse_flipper_dah_down();
}

static void morse_flipper_append_text(char* dst, size_t dst_sz, const char* add) {
    size_t len;
    size_t add_len;

    if(dst == NULL || dst_sz < 2U || add == NULL || add[0] == '\0') return;

    len = strlen(dst);
    add_len = strlen(add);
    if(add_len >= dst_sz) {
        memcpy(dst, add + add_len - (dst_sz - 1U), dst_sz - 1U);
        dst[dst_sz - 1U] = '\0';
        return;
    }

    if(len + add_len >= dst_sz) {
        size_t drop = len + add_len - (dst_sz - 1U);
        memmove(dst, dst + drop, len - drop + 1U);
        len = strlen(dst);
    }

    memcpy(dst + len, add, add_len + 1U);
}

static void morse_flipper_dec_drain( MorseFlipperCwDecoder* decoder, char* out, size_t out_sz) {
    if(decoder == NULL || out == NULL || out_sz < 2U) return;

    if(morse_flipper_cw_decoder_output(decoder)[0] != '\0') {
        morse_flipper_append_text(out, out_sz, morse_flipper_cw_decoder_output(decoder));
        morse_flipper_cw_decoder_clear_output(decoder);
    }
}

static char morse_flipper_tx_symbol(const MorseFlipperApp* app) {
    if(app->note_src[1] != 0U) return '.';
    if(app->note_src[2] != 0U) return '-';
    if(app->note_src[0] != 0U) return 'K';
    return '?';
}

static bool morse_flipper_tx_decoder_allowed(const MorseFlipperApp* app) {
    if(app == NULL) return false;
    if(app->trainer_playback_active || app->straight_playback_active) return false;
    if(app->screen == MorseFlipperScreenSession && !morse_flipper_session_repeat_active(app)) return false;
    if(app->screen == MorseFlipperScreenRf && !MORSE_FLIPPER_RF_LIVE_DECODERS) return false;
    return true;
}

static void morse_flipper_feed_tx_edge(MorseFlipperApp* app, bool level, uint32_t now_ms) {
    uint32_t dt;

    if(app == NULL) return;
    if(level == app->rf_tx_level) return;

    if(app->rf_tx_edge_at != 0U) {
        dt = now_ms - app->rf_tx_edge_at;
        if(dt != 0U && morse_flipper_tx_decoder_allowed(app)) {
            if(app->rf_tx_level) {
                morse_flipper_cw_decoder_feed_mark(&app->tx_decoder, (uint16_t)dt);
            } else {
                morse_flipper_cw_decoder_feed_space(&app->tx_decoder, (uint16_t)dt);
            }
            morse_flipper_dec_drain(&app->tx_decoder, app->rf_tx_text, sizeof(app->rf_tx_text));
        }
    }

    app->rf_tx_level = level;
    app->rf_tx_edge_at = now_ms;
    app->rf_tx_gap_flushed = level;
    morse_flipper_rf_handle_tx(&app->rf, level, morse_flipper_tx_symbol(app));
}

static void morse_flipper_feed_gpio_edge(MorseFlipperApp* app, bool level, uint32_t now_ms) {
    uint32_t dt;

    if(app == NULL) return;
    if(level == app->gpio_level) return;

    if(app->gpio_edge_at != 0U) {
        dt = now_ms - app->gpio_edge_at;
        if(dt != 0U) {
            if(app->gpio_level) {
                morse_flipper_cw_decoder_feed_mark(&app->gpio_decoder, (uint16_t)dt);
            } else {
                morse_flipper_cw_decoder_feed_space(&app->gpio_decoder, (uint16_t)dt);
            }
            morse_flipper_dec_drain(&app->gpio_decoder, app->gpio_text, sizeof(app->gpio_text));
        }
    }

    app->gpio_level = level;
    app->gpio_edge_at = now_ms;
    app->gpio_gap_flushed = level;
}

static void morse_flipper_feed_sk_mark(MorseFlipperApp* app, uint16_t mark_ms) {
    char elem;

    if(app == NULL || mark_ms == 0U) return;

    elem = mark_ms >= (morse_flipper_current_dit_ms(app) * 2U) ? '-' : '.';
    morse_flipper_straight_trainer_feed(&app->straight_trainer, elem, mark_ms);
    app->straight_last_input_at = furi_get_tick();

    if(strlen(morse_flipper_straight_trainer_answer(&app->straight_trainer)) >=
       strlen(morse_flipper_straight_trainer_target_morse(&app->straight_trainer))) {
        app->sk_wait = false;
        app->sk_done = true;
        app->straight_trainer.active = false;
        morse_trainer_note_straight_attempt(
            &app->straight_stats,
            morse_flipper_straight_trainer_target_char(&app->straight_trainer),
            morse_flipper_straight_trainer_average_mark_error_ms(&app->straight_trainer),
            morse_flipper_straight_trainer_average_drift_percent(&app->straight_trainer),
            morse_flipper_straight_trainer_target_morse(&app->straight_trainer),
            morse_flipper_straight_trainer_answer(&app->straight_trainer));
    }
}


static void morse_flipper_drain_keyer_events(MorseFlipperApp* app) {
    MorseKeyerEvent event;

    while(morse_keyer_pop_event(&app->keyer, &event)) {
        if((app->screen == MorseFlipperScreenTrainer ||
            app->screen == MorseFlipperScreenSession) &&
           event.type == MorseKeyerEventPress &&
           morse_trainer_phase(&app->trainer) == MorseTrainerPhaseRepeat) {
            morse_trainer_feed_element( &app->trainer, event.paddle == MorseKeyerPaddleDit ? '.' : '-');
        }

        if(app->screen == MorseFlipperScreenSession &&
           morse_trainer_phase(&app->trainer) == MorseTrainerPhaseRepeat) {
            app->session_last_input_at = furi_get_tick();
        }

        morse_flipper_set_note_source(
            app,
            morse_flipper_note_for_paddle(event.paddle),
            morse_flipper_note_source_for_paddle(event.paddle),
            event.type == MorseKeyerEventPress);
    }
}

static void morse_flipper_set_paddle_source( MorseFlipperApp* app, uint8_t paddle, uint32_t source_mask, bool active, uint32_t now_ms) {
    if(paddle >= MorseKeyerPaddleCount) {
        return;
    }

    uint32_t before = app->paddle_sources[paddle];
    uint32_t after = active ? (before | source_mask) : (before & ~source_mask);

    if(before == after) {
        return;
    }

    app->paddle_sources[paddle] = after;
    morse_keyer_paddle_event(&app->keyer, paddle, after != 0U, now_ms);
    morse_keyer_tick(&app->keyer, now_ms);
    morse_flipper_drain_keyer_events(app);
}

static void morse_flipper_key_evt( MorseFlipperApp* app, const InputEvent* event) {
    uint32_t now_ms = furi_get_tick();
    MorseFlipperInputGate g = morse_flipper_input_gate(app);
    bool btn_src = g.btn;
    bool btn_str = g.btn_str;
    bool btn_pad = g.btn_pad;

    if(btn_src && event->key == InputKeyLeft) {
        if(event->type == InputTypePress) {
            app->left_down = true;
        } else if(event->type == InputTypeRelease) {
            app->left_down = false;
        } else if(event->type == InputTypeLong) {
            morse_flipper_leave_live_screen(app, now_ms);
        }
        return;
    }

    if(btn_src && event->key == InputKeyOk) {
        if(event->type == InputTypePress) {
            app->ok_down = true;
            if(btn_str) {
                morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_BTN, true);
            } else if(btn_pad) {
                morse_flipper_resync_button_paddles(app, now_ms);
            }
        } else if(event->type == InputTypeRelease) {
            app->ok_down = false;
            if(btn_str) {
                morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_BTN, false);
            } else if(btn_pad) {
                morse_flipper_resync_button_paddles(app, now_ms);
            }
        }
        return;
    }

    if(btn_pad && event->key == InputKeyBack) {
        if(event->type == InputTypePress) {
            app->back_down = true;
            morse_flipper_resync_button_paddles(app, now_ms);
        } else if(event->type == InputTypeRelease) {
            app->back_down = false;
            morse_flipper_resync_button_paddles(app, now_ms);
        }
        return;
    }

    if(g.back_exit && event->key == InputKeyBack &&
       (event->type == InputTypeShort || event->type == InputTypeLong)) {
        morse_flipper_leave_live_screen(app, now_ms);
        return;
    }

    if(event->key == InputKeyUp && event->type == InputTypeShort) {
        morse_flipper_toggle_source(app);
        return;
    }

    if(event->key == InputKeyDown && event->type == InputTypeShort) {
        morse_flipper_cycle_mode(app);
        return;
    }

    if(event->key == InputKeyDown && event->type == InputTypeLong) {
        morse_flipper_toggle_handedness(app);
        return;
    }

    if(event->key == InputKeyRight &&
       (event->type == InputTypeShort || event->type == InputTypeLong)) {
        morse_flipper_scene_back(app);
    }
}

static void morse_flipper_refresh_keyer(MorseFlipperApp* app, uint32_t now_ms) {
    morse_keyer_reset(&app->keyer);
    morse_flipper_drain_keyer_events(app);
    morse_keyer_set_mode(&app->keyer, morse_flipper_current_keyer_mode(app));
    morse_keyer_set_dit_duration(&app->keyer, morse_flipper_current_dit_ms(app));

    for(uint8_t paddle = 0; paddle < MorseKeyerPaddleCount; paddle++) {
        if(app->paddle_sources[paddle] != 0U) {
            morse_keyer_paddle_event(&app->keyer, paddle, true, now_ms);
        }
    }

    morse_keyer_tick(&app->keyer, now_ms);
    morse_flipper_drain_keyer_events(app);
}

static void morse_flipper_set_pc_mode(MorseFlipperApp* app, uint8_t mode) {
    const char* prod = "Morse Flipper Kbd";

    if(mode > MorseFlipperPcModeMidi) {
        mode = MorseFlipperPcModeOff;
    }

    if(app->pc_mode == mode) {
        return;
    }

    morse_flipper_release_all_notes(app);

    if(app->pc_mode == MorseFlipperPcModeKeyboard) {
        furi_hal_hid_kb_release_all();
    } else if(app->pc_mode == MorseFlipperPcModeMouse) {
        morse_flipper_release_mouse_buttons();
    }

    if(mode == MorseFlipperPcModeOff) {
        morse_usb_midi_set_rx_callback(NULL);
        morse_usb_midi_set_context(NULL);
        if(app->previous_usb_config != NULL) {
            furi_check(furi_hal_usb_set_config(app->previous_usb_config, NULL));
            app->previous_usb_config = NULL;
        }
        app->pc_mode = MorseFlipperPcModeOff;
        app->transport_connected = false;
        app->midi_rx_pending = false;
        morse_flipper_clear_vail_overrides(app);
        morse_flipper_refresh_keyer(app, furi_get_tick());
        return;
    }

    if(app->previous_usb_config == NULL) {
        app->previous_usb_config = furi_hal_usb_get_config();
    }

    if(mode == MorseFlipperPcModeMidi) {
        morse_usb_midi_set_context(app);
        morse_usb_midi_set_rx_callback(morse_flipper_midi_rx_ready);
        furi_check(furi_hal_usb_set_config(NULL, NULL));
        furi_delay_ms(150U);
        furi_check(furi_hal_usb_set_config(&morse_usb_midi_interface, NULL));
    } else {
        if(mode == MorseFlipperPcModeMouse) prod = "Morse Flipper Mouse";
        snprintf(
            app->hid_cfg.product,
            sizeof(app->hid_cfg.product),
            "%s",
            prod);
        morse_usb_midi_set_rx_callback(NULL);
        morse_usb_midi_set_context(NULL);
        app->midi_rx_pending = false;
        furi_check(furi_hal_usb_set_config(NULL, NULL));
        furi_delay_ms(150U);
        furi_check(furi_hal_usb_set_config(&usb_hid, &app->hid_cfg));
        morse_flipper_clear_vail_overrides(app);
    }

    app->pc_mode = mode;
    app->transport_connected = false;
    morse_flipper_refresh_keyer(app, furi_get_tick());
}

static void morse_flipper_handle_midi_rx(MorseFlipperApp* app) {
    uint8_t buffer[64];
    size_t size = morse_usb_midi_rx(buffer, sizeof(buffer));

    for(size_t offset = 0; offset + 3U < size; offset += 4U) {
        uint8_t status = buffer[offset + 1U] & 0xF0U;

        if(status == 0xB0U) {
            uint8_t control = buffer[offset + 2U];
            uint8_t value = buffer[offset + 3U];

            if(control == 1U) {
                morse_flipper_apply_vail_speed(app, value);
            } else if(control == 2U) {
                morse_flipper_apply_vail_tone(app, value);
            }
        } else if(status == 0xC0U) {
            morse_flipper_apply_vail_mode(app, buffer[offset + 2U]);
        }
    }
}

static void morse_flipper_sync_gpio_inputs(MorseFlipperApp* app, uint32_t now_ms) {
    bool straight_active = false;
    bool dit_active = false;
    bool dah_active = false;

    if(app->in_src == MorseFlipperInputSourceStraight) {
        straight_active = morse_flipper_straight_down();
    } else if(app->in_src == MorseFlipperInputSourcePaddle) {
        dit_active = morse_flipper_logical_dit_down(app);
        dah_active = morse_flipper_logical_dah_down(app);
    }

    if(morse_flipper_training_playback_active(app) ||
       (app->screen == MorseFlipperScreenSession && !morse_flipper_session_repeat_active(app))) {
        straight_active = false;
        dit_active = false;
        dah_active = false;
    }

    morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_GPIO, straight_active);
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDit, MORSE_PADDLE_SOURCE_GPIO_DIT, dit_active, now_ms);
    morse_flipper_set_paddle_source( app, MorseKeyerPaddleDah, MORSE_PADDLE_SOURCE_GPIO_DAH, dah_active, now_ms);
}

static uint8_t morse_flipper_read_input_mask(const MorseFlipperApp* app) {
    uint8_t mask = 0U;

    if(app->left_down) {
        mask |= 1U << 0;
    }
    if(app->ok_down) {
        mask |= 1U << 1;
    }
    if(app->back_down) {
        mask |= 1U << 2;
    }
    if(morse_flipper_straight_down()) {
        mask |= 1U << 3;
    }
    if(morse_flipper_dah_down()) {
        mask |= 1U << 4;
    }
    if(morse_flipper_dit_down()) {
        mask |= 1U << 5;
    }

    return mask;
}

static const char* morse_flipper_input_line(const MorseFlipperApp* app, char* buf, size_t buf_sz) {
    size_t n = snprintf(buf, buf_sz, "raw ");

    if(app->input_mask & (1U << 0)) {
        n += snprintf(buf + n, buf_sz - n, "lt ");
    }
    if(app->input_mask & (1U << 1)) {
        n += snprintf(buf + n, buf_sz - n, "ok ");
    }
    if(app->input_mask & (1U << 2)) {
        n += snprintf(buf + n, buf_sz - n, "bk ");
    }
    if(app->input_mask & (1U << 3)) {
        n += snprintf(buf + n, buf_sz - n, "%s ", morse_flipper_gpio_name(app->gpio_straight_idx));
    }
    if((app->input_mask & (1U << 4)) && app->gpio_dah_idx != app->gpio_straight_idx) {
        n += snprintf(buf + n, buf_sz - n, "%s ", morse_flipper_gpio_name(app->gpio_dah_idx));
    }
    if((app->input_mask & (1U << 5)) && app->gpio_dit_idx != app->gpio_straight_idx &&
       app->gpio_dit_idx != app->gpio_dah_idx) {
        n += snprintf(buf + n, buf_sz - n, "%s ", morse_flipper_gpio_name(app->gpio_dit_idx));
    }

    if(n == 4U) {
        snprintf(buf, buf_sz, "raw -");
    }

    return buf;
}

static void morse_flipper_tone_nudge(MorseFlipperApp* app, int dir) {
    int idx = (int)app->tone_idx + dir;

    if(idx < 0) {
        idx = 0;
    }
    if(idx >= (int)COUNT_OF(morse_flipper_tones)) {
        idx = (int)COUNT_OF(morse_flipper_tones) - 1;
    }
    if(idx == (int)app->tone_idx) {
        return;
    }

    app->tone_idx = (uint8_t)idx;
    app->prev_n = MORSE_FLIPPER_PREVIEW_TICKS;

    if(app->tone_on && app->sp_owned && furi_hal_speaker_is_mine()) {
        furi_hal_speaker_start(morse_flipper_current_tone(app)->hz, MORSE_FLIPPER_VOLUME);
    }

    morse_flipper_save_config(app);
    morse_flipper_update_sidetone(app);
    morse_flipper_view_dirty(app);
}

static void morse_flipper_poll(MorseFlipperApp* app) {
    uint32_t now_ms = furi_get_tick();
    bool old_tone = app->tone_on;
    bool old_busy = app->sp_busy;
    uint8_t old_mask = app->input_mask;
    bool old_transport = app->transport_connected;
    bool raw_straight;
    bool tx_now;

    if(app->pc_mode == MorseFlipperPcModeMidi && app->midi_rx_pending) {
        app->midi_rx_pending = false;
        morse_flipper_handle_midi_rx(app);
    }

    if(app->screen == MorseFlipperScreenSession && app->session_started && app->session_log_pending &&
       !morse_trainer_session_active(&app->trainer) &&
       morse_trainer_phase(&app->trainer) == MorseTrainerPhaseDone) {
        if(morse_trainer_session_completed(&app->trainer)) {
            morse_trainer_append_session_log(&app->trainer);
            morse_trainer_load_session_lines(&app->session_lines);
            app->session_line_idx =
                app->session_lines.count == 0U ? 0U : (app->session_lines.count - 1U);
        }
        app->session_log_pending = false;
    }

    if(app->screen == MorseFlipperScreenSession && app->session_started &&
       morse_trainer_phase(&app->trainer) == MorseTrainerPhaseRepeat) {
        morse_trainer_tick( &app->trainer, MORSE_FLIPPER_POLL_MS, (uint32_t)app->trainer_to_s * 1000U);
    }

    morse_flipper_tick_session(app, now_ms);
    morse_flipper_tick_straight(app, now_ms);

    app->transport_connected = morse_flipper_transport_connected(app);
    if(!old_transport && app->transport_connected) {
        morse_flipper_resync_transport_notes(app);
    }

    app->input_mask = morse_flipper_read_input_mask(app);
    morse_flipper_sync_gpio_inputs(app, now_ms);
    morse_keyer_tick(&app->keyer, now_ms);
    morse_flipper_drain_keyer_events(app);

    raw_straight = morse_flipper_straight_down();
    morse_flipper_feed_gpio_edge(app, raw_straight, now_ms);

    if(!app->gpio_level && !app->gpio_gap_flushed && app->gpio_edge_at != 0U) {
        uint32_t gap = now_ms - app->gpio_edge_at;
        if(gap >= (morse_flipper_current_dit_ms(app) * 5U) / 2U) {
            morse_flipper_cw_decoder_feed_space(&app->gpio_decoder, (uint16_t)gap);
            morse_flipper_dec_drain(&app->gpio_decoder, app->gpio_text, sizeof(app->gpio_text));
            app->gpio_gap_flushed = true;
        }
    }

    raw_straight = morse_flipper_straight_answer_down(app);
    if(app->screen == MorseFlipperScreenStraight && !raw_straight &&
       app->straight_mark_started_at != 0U && app->sk_down) {
        app->sk_down = false;
        morse_flipper_feed_sk_mark(app, (uint16_t)(now_ms - app->straight_mark_started_at));
        app->straight_mark_started_at = 0U;
    } else if(app->screen == MorseFlipperScreenStraight && raw_straight &&
              !app->sk_down && app->sk_wait) {
        app->sk_down = true;
        app->straight_mark_started_at = now_ms;
    }

    tx_now = morse_flipper_any_active_notes(app);
    morse_flipper_feed_tx_edge(app, tx_now, now_ms);
    if(!app->rf_tx_level && !app->rf_tx_gap_flushed && app->rf_tx_edge_at != 0U) {
        uint32_t gap = now_ms - app->rf_tx_edge_at;
        if(gap >= (morse_flipper_current_dit_ms(app) * 5U) / 2U) {
            if(morse_flipper_tx_decoder_allowed(app)) {
                morse_flipper_cw_decoder_feed_space(&app->tx_decoder, (uint16_t)gap);
                morse_flipper_dec_drain(&app->tx_decoder, app->rf_tx_text, sizeof(app->rf_tx_text));
            }
            app->rf_tx_gap_flushed = true;
        }
    }

    morse_flipper_tick_live_rf(app, now_ms);
#if MORSE_FLIPPER_RF_LIVE_DECODERS
    morse_flipper_radio_drain_rx(&app->radio);
#endif
    morse_flipper_update_sidetone(app);
    morse_flipper_sync_backlight(app, now_ms);

    if(old_tone != app->tone_on || old_busy != app->sp_busy || old_mask != app->input_mask ||
       old_transport != app->transport_connected) {
        morse_flipper_view_dirty(app);
    }
}


static bool morse_flipper_live_input(InputEvent* event, void* ctx) {
    MorseFlipperApp* app = ctx;
    uint32_t now_ms = furi_get_tick();

    if(app->screen == MorseFlipperScreenHelp) {
        if(event->key == InputKeyLeft && (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->help_topic = app->help_topic == 0U ? (MorseFlipperHelpCount - 1U) : app->help_topic - 1U;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyRight && (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->help_topic = (app->help_topic + 1U) % MorseFlipperHelpCount;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyBack && (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenAbout) {
        if(event->key == InputKeyBack && (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenPcKeys) {
        if(event->key == InputKeyUp &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->pc_keys_row = app->pc_keys_row == 0U ? 1U : 0U;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyDown &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->pc_keys_row = app->pc_keys_row == 0U ? 1U : 0U;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyLeft &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_cycle_pc_key_preset(app, -1);
            return true;
        }

        if(event->key == InputKeyRight &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_cycle_pc_key_preset(app, 1);
            return true;
        }

        if((event->key == InputKeyOk || event->key == InputKeyBack) &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenPc) {
        if(event->key == InputKeyLeft || event->key == InputKeyOk || event->key == InputKeyBack) {
            morse_flipper_key_evt(app, event);
            return true;
        }

        if(event->key == InputKeyUp && event->type == InputTypeShort) {
            morse_flipper_cycle_pc_mode(app, -1);
            return true;
        }

        if(event->key == InputKeyDown && event->type == InputTypeShort) {
            morse_flipper_cycle_pc_mode(app, 1);
            return true;
        }

        if(event->key == InputKeyRight && event->type == InputTypeLong) {
            if(app->pc_mode == MorseFlipperPcModeKeyboard)
                morse_flipper_scene_open(app, MorseFlipperScenePcKeys);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenTrainer) {
        if(event->key == InputKeyOk && event->type == InputTypeShort) {
            morse_flipper_scene_open(app, MorseFlipperSceneSession);
            return true;
        }

        if(event->key == InputKeyUp &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->trainer_row = app->trainer_row == 0U ? 3U : (app->trainer_row - 1U);
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyDown &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            app->trainer_row = (app->trainer_row + 1U) % 4U;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyLeft &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_cycle_train(app, -1);
            return true;
        }

        if(event->key == InputKeyRight &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_cycle_train(app, 1);
            return true;
        }

        if(event->key == InputKeyBack &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            app->trainer_playback_active = false;
            app->trainer_playback_mark = false;
            app->session_log_pending = false;
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenStraight) {
        if(morse_flipper_live_back_exits(app) && event->key == InputKeyBack &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        if(app->sk_wait && app->in_src == MorseFlipperInputSourceButtons &&
           event->key == InputKeyOk) {
            if(event->type == InputTypePress) {
                app->ok_down = true;
                app->sk_down = true;
                app->straight_mark_started_at = now_ms;
                morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_BTN, true);
                morse_flipper_view_dirty(app);
            } else if(event->type == InputTypeRelease) {
                uint16_t dt = 0U;

                app->ok_down = false;
                morse_flipper_set_note_source(app, 0U, MORSE_SOURCE_STRAIGHT_BTN, false);
                if(app->sk_down && app->straight_mark_started_at != 0U)
                    dt = (uint16_t)(now_ms - app->straight_mark_started_at);
                app->sk_down = false;
                app->straight_mark_started_at = 0U;
                if(dt != 0U) {
                    morse_flipper_feed_sk_mark(app, dt);
                    morse_flipper_view_dirty(app);
                }
            }
            return true;
        }

        if(!app->straight_playback_active && !app->sk_wait && event->key == InputKeyOk &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_start_straight_round(app, now_ms);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenSession) {
        MorseFlipperInputGate g = morse_flipper_input_gate(app);

        if(morse_flipper_session_repeat_active(app)) {
            if(event->key == InputKeyLeft && event->type == InputTypeLong) {
                morse_flipper_leave_session(app, now_ms);
                return true;
            }

            if(event->key == InputKeyLeft && morse_flipper_session_left_exit_active(app)) {
                morse_flipper_key_evt(app, event);
                return true;
            }

            if(event->key == InputKeyOk && g.btn) {
                morse_flipper_key_evt(app, event);
                return true;
            }

            if(event->key == InputKeyBack && g.back_key) {
                morse_flipper_key_evt(app, event);
                return true;
            }

            if(g.back_exit && event->key == InputKeyBack &&
               (event->type == InputTypeShort || event->type == InputTypeLong)) {
                morse_flipper_leave_session(app, now_ms);
                return true;
            }

            return false;
        }

        if(morse_flipper_session_running_view(app)) {
            if(event->key == InputKeyLeft && event->type == InputTypeLong) {
                morse_flipper_leave_session(app, now_ms);
                return true;
            }

            if(event->key == InputKeyLeft && morse_flipper_session_left_exit_active(app)) {
                morse_flipper_key_evt(app, event);
                return true;
            }

            if(event->key == InputKeyBack && g.back_key) {
                morse_flipper_key_evt(app, event);
                return true;
            }

            if(g.back_exit && event->key == InputKeyBack &&
               (event->type == InputTypeShort || event->type == InputTypeLong)) {
                morse_flipper_leave_session(app, now_ms);
                return true;
            }

            if(event->key == InputKeyOk && g.btn) {
                return true;
            }
        }

        if(event->key == InputKeyOk && event->type == InputTypeShort &&
           !morse_trainer_session_active(&app->trainer) && !app->trainer_playback_active) {
            morse_flipper_start_session(app, now_ms);
            return true;
        }

        if(event->key == InputKeyOk && event->type == InputTypeLong &&
           !morse_trainer_session_active(&app->trainer) && !app->trainer_playback_active) {
            morse_flipper_scene_open(app, MorseFlipperSceneStraight);
            return true;
        }

        if(event->key == InputKeyBack &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_leave_session(app, now_ms);
            return true;
        }

        if(event->key == InputKeyUp && event->type == InputTypeLong &&
           !morse_trainer_session_active(&app->trainer) && !app->trainer_playback_active) {
            morse_trainer_load_session_lines(&app->session_lines);
            app->session_line_idx = app->session_lines.count == 0U ? 0U : (app->session_lines.count - 1U);
            morse_flipper_scene_open(app, MorseFlipperSceneBrowse);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenBrowse) {
        if(event->key == InputKeyUp &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat) &&
           app->session_lines.count != 0U) {
            app->session_line_idx = app->session_line_idx == 0U ?
                                        (app->session_lines.count - 1U) :
                                        (app->session_line_idx - 1U);
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyDown &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat) &&
           app->session_lines.count != 0U) {
            app->session_line_idx = (app->session_line_idx + 1U) % app->session_lines.count;
            morse_flipper_view_dirty(app);
            return true;
        }

        if((event->key == InputKeyRight || event->key == InputKeyBack) &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenRf) {
        if(app->rf_man) {
            if(event->key == InputKeyLeft &&
               (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
                app->rf_edit_digit = app->rf_edit_digit == 0U ? 5U : (app->rf_edit_digit - 1U);
                morse_flipper_view_dirty(app);
                return true;
            }

            if(event->key == InputKeyRight &&
               (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
                app->rf_edit_digit = (app->rf_edit_digit + 1U) % 6U;
                morse_flipper_view_dirty(app);
                return true;
            }

            if(event->key == InputKeyUp &&
               (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
                char* ch = &app->rf_edit_khz[app->rf_edit_digit];
                *ch = (*ch >= '9' || *ch < '0') ? '0' : (char)(*ch + 1);
                morse_flipper_view_dirty(app);
                return true;
            }

            if(event->key == InputKeyDown &&
               (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
                char* ch = &app->rf_edit_khz[app->rf_edit_digit];
                *ch = (*ch <= '0' || *ch > '9') ? '9' : (char)(*ch - 1);
                morse_flipper_view_dirty(app);
                return true;
            }

            if(event->key == InputKeyOk &&
               (event->type == InputTypeShort || event->type == InputTypeLong)) {
                morse_flipper_rf_set_manual_khz(&app->rf, app->rf_edit_khz);
                app->rf_man = false;
                morse_flipper_view_dirty(app);
                return true;
            }

            if(event->key == InputKeyBack &&
               (event->type == InputTypeShort || event->type == InputTypeLong)) {
                app->rf_man = false;
                morse_flipper_view_dirty(app);
                return true;
            }

            return false;
        }

        if(app->rf_live) {
            morse_flipper_key_evt(app, event);
            return true;
        }

        if(event->key == InputKeyLeft &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_rf_step(&app->rf, -1);
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyRight &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_rf_step(&app->rf, 1);
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyUp &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            morse_flipper_rf_next_band(&app->rf);
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyDown &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            app->rf_live = true;
            app->rf_tail_at = 0U;
            app->rf_tx_edge_at = 0U;
            app->rf_tx_level = false;
            app->rf_tx_gap_flushed = true;
            app->rf_rx_text[0] = '\0';
            app->rf_tx_text[0] = '\0';
            morse_flipper_rf_reset_live(&app->rf);
            morse_flipper_cw_decoder_init(&app->rf_decoder, morse_flipper_current_dit_ms(app));
            morse_flipper_cw_decoder_init(&app->tx_decoder, morse_flipper_current_dit_ms(app));
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyOk &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            strncpy(app->rf_edit_khz, morse_flipper_rf_manual_khz_text(&app->rf), 6U);
            app->rf_edit_khz[6] = '\0';
            app->rf_edit_digit = 0U;
            app->rf_man = true;
            morse_flipper_view_dirty(app);
            return true;
        }

        if(event->key == InputKeyBack &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }

        return false;
    }

    if(app->screen == MorseFlipperScreenRun) {
        morse_flipper_key_evt(app, event);
        return true;
    }

    if(app->screen == MorseFlipperScreenTrace) {
        morse_flipper_key_evt(app, event);
        return true;
    }

    if(app->screen == MorseFlipperScreenHome) {
        if(event->type == InputTypePress) {
            if(event->key == InputKeyLeft) {
                morse_flipper_tone_nudge(app, -1);
                return true;
            } else if(event->key == InputKeyRight) {
                morse_flipper_tone_nudge(app, 1);
                return true;
            }
        }

        if(event->key == InputKeyUp && event->type == InputTypeShort) {
            morse_flipper_toggle_source(app);
            return true;
        }

        if(event->key == InputKeyDown && event->type == InputTypeShort) {
            morse_flipper_cycle_mode(app);
            return true;
        }

        if(event->key == InputKeyDown && event->type == InputTypeLong) {
            morse_flipper_toggle_handedness(app);
            return true;
        }

        if(event->key == InputKeyBack &&
           (event->type == InputTypeShort || event->type == InputTypeLong)) {
            morse_flipper_scene_back(app);
            return true;
        }
    }

    return false;
}

static bool morse_flipper_custom_evt(void* context, uint32_t event) {
    MorseFlipperApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool morse_flipper_back_evt(void* context) {
    MorseFlipperApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void morse_flipper_tick_callback(void* context) {
    MorseFlipperApp* app = context;

    morse_flipper_poll(app);
    morse_flipper_tick_trainer_playback(app, furi_get_tick());

    if(app->prev_n > 0U) {
        app->prev_n--;
        morse_flipper_update_sidetone(app);
    }

    scene_manager_handle_tick_event(app->scene_manager);
}
