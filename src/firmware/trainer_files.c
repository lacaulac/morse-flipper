#include "trainer.h"
#include "trainer_files.h"

#ifdef MORSE_FLIPPER_FAP
#include <storage/storage.h>
#include <furi.h>
#else
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#endif

#include <string.h>

static const char* morse_trainer_custom_path_value = "/ext/ham/flipper-cw-custom-characters.txt";
static const char* morse_trainer_session_log_path_value = "/ext/ham/flipper-cw-session-results.txt";
static const char* morse_trainer_custom_defaults =
    "numbers=0123456789\n"
    "dits=EISH5\n"
    "more dits=EISH5AUVNDB\n";

const char* morse_trainer_custom_chars_path(void) {
    return morse_trainer_custom_path_value;
}

const char* morse_trainer_session_log_path(void) {
    return morse_trainer_session_log_path_value;
}

#ifdef MORSE_FLIPPER_FAP
static bool morse_trainer_read_custom_text(char* buf, size_t buf_sz) {
    Storage* storage;
    File* file;
    uint16_t got = 0U;

    if(buf == NULL || buf_sz == 0U) {
        return false;
    }

    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    storage_common_mkdir(storage, "/ext/ham");

    if(!storage_file_open(
           file, morse_trainer_custom_path_value, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_open(
               file, morse_trainer_custom_path_value, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(file, morse_trainer_custom_defaults, strlen(morse_trainer_custom_defaults));
        }
        storage_file_close(file);
        storage_file_open(file, morse_trainer_custom_path_value, FSAM_READ, FSOM_OPEN_EXISTING);
    }

    got = storage_file_read(file, buf, buf_sz - 1U);
    buf[got] = '\0';
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return got != 0U;
}
#else
static bool morse_trainer_read_custom_text(char* buf, size_t buf_sz) {
    FILE* f;
    size_t got;

    if(buf == NULL || buf_sz == 0U) {
        return false;
    }

    f = fopen(morse_trainer_custom_path_value, "rb");
    if(f == NULL) {
        mkdir("/ext", 0777);
        mkdir("/ext/ham", 0777);
        f = fopen(morse_trainer_custom_path_value, "wb");
        if(f != NULL) {
            fwrite(morse_trainer_custom_defaults, 1, strlen(morse_trainer_custom_defaults), f);
            fclose(f);
        }
        f = fopen(morse_trainer_custom_path_value, "rb");
    }

    if(f == NULL) {
        return false;
    }

    got = fread(buf, 1, buf_sz - 1U, f);
    buf[got] = '\0';
    fclose(f);
    return got != 0U;
}
#endif

bool morse_trainer_load_custom_sets(MorseTrainerCustomSets* sets) {
    char buf[512];
    char* line;
    char* next;
    char* eq;

    if(sets == NULL) {
        return false;
    }

    memset(sets, 0, sizeof(*sets));
    if(!morse_trainer_read_custom_text(buf, sizeof(buf))) {
        return false;
    }

    line = buf;
    while(line != NULL && *line != '\0' && sets->count < MORSE_TRAINER_CUSTOM_SET_CAP) {
        next = strchr(line, '\n');
        if(next != NULL) {
            *next++ = '\0';
        }

        eq = strchr(line, '=');
        if(eq != NULL) {
            *eq++ = '\0';
            if(line[0] != '\0' && eq[0] != '\0') {
                strncpy(sets->sets[sets->count].name, line, MORSE_TRAINER_CUSTOM_NAME_CAP - 1U);
                strncpy(
                    sets->sets[sets->count].chars, eq, MORSE_TRAINER_CHARSET_CAP - 1U);
                sets->count++;
            }
        }

        line = next;
    }

    return sets->count != 0U;
}

bool morse_trainer_append_session_log(const MorseTrainer* trainer) {
    char line[192];

    if(trainer == NULL) {
        return false;
    }

    snprintf(
        line,
        sizeof(line),
        "lesson=%u speed=%u groups=%u size=%u score=%u fails=%u aborted=%u missed=%u\n",
        (unsigned)morse_trainer_lesson(trainer),
        (unsigned)(trainer->local_dit_ms == 0U ? 0U : (1200U / trainer->local_dit_ms)),
        (unsigned)morse_trainer_session_total(trainer),
        (unsigned)morse_trainer_group_size(trainer),
        (unsigned)morse_trainer_session_average_score(trainer),
        (unsigned)morse_trainer_session_fail_count(trainer),
        morse_trainer_session_aborted(trainer) ? 1U : 0U,
        (unsigned)morse_trainer_session_consecutive_missed(trainer));

#ifdef MORSE_FLIPPER_FAP
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok;

    storage_common_mkdir(storage, "/ext/ham");
    ok = storage_file_open(
        file, morse_trainer_session_log_path_value, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok) {
        storage_file_write(file, line, strlen(line));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
#else
    FILE* f = fopen(morse_trainer_session_log_path_value, "ab");
    if(f == NULL) {
        mkdir("/ext", 0777);
        mkdir("/ext/ham", 0777);
        f = fopen(morse_trainer_session_log_path_value, "ab");
    }
    if(f == NULL) {
        return false;
    }
    fwrite(line, 1, strlen(line), f);
    fclose(f);
    return true;
#endif
}

bool morse_trainer_load_session_lines(MorseTrainerSessionLines* lines) {
    char buf[1024];
    char* line;
    char* next;
    uint8_t slot;
    uint8_t i;

    if(lines == NULL) {
        return false;
    }

    memset(lines, 0, sizeof(*lines));

#ifdef MORSE_FLIPPER_FAP
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint16_t got = 0U;

    if(storage_file_open(file, morse_trainer_session_log_path_value, FSAM_READ, FSOM_OPEN_EXISTING)) {
        got = storage_file_read(file, buf, sizeof(buf) - 1U);
        buf[got] = '\0';
    } else {
        buf[0] = '\0';
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
#else
    FILE* f = fopen(morse_trainer_session_log_path_value, "rb");
    size_t got = 0U;

    if(f != NULL) {
        got = fread(buf, 1, sizeof(buf) - 1U, f);
        fclose(f);
    }
    buf[got] = '\0';
#endif

    line = buf;
    while(line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if(next != NULL) {
            *next++ = '\0';
        }

        if(lines->count < 8U) {
            slot = lines->count++;
        } else {
            for(i = 1U; i < 8U; i++) {
                strcpy(lines->lines[i - 1U], lines->lines[i]);
            }
            slot = 7U;
        }

        strncpy(lines->lines[slot], line, sizeof(lines->lines[slot]) - 1U);
        lines->lines[slot][sizeof(lines->lines[slot]) - 1U] = '\0';
        line = next;
    }

    return lines->count != 0U;
}
