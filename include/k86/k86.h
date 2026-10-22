/*
 * libk86 - C API for the Attack Shark K86 keyboard (both firmwares: YC3121 3151:4015 and
 * YC3123 3151:5002). Everything k86ctl can do, for other programs:
 *
 *     #include <k86/k86.h>         // cc app.c $(pkg-config --cflags --libs k86)
 *
 *     k86_keyboard* kb = k86_open(NULL, 0);          // first K86 found
 *     if (!kb) { fprintf(stderr, "%s\n", k86_last_error()); return 1; }
 *     k86_set_key(kb, 0, "CapsLock", "LCtrl");        // same syntax as `k86ctl keymap set`
 *     k86_set_macro(kb, 0, "+LCtrl/5 C/20 -LCtrl", 1); // same syntax as `k86ctl macro set`
 *     k86_light l = {0};
 *     l.mode = (uint8_t)k86_light_mode(kb, "static", 0);
 *     l.brightness = 4; l.color = 0x00FF00;
 *     k86_set_light(kb, &l);
 *     k86_close(kb);
 *
 * Conventions
 *  - Functions returning int return 0 on success and -1 on error; k86_last_error() then
 *    describes the error (per thread, valid until the next libk86 call on that thread).
 *  - Keys are named like in `k86ctl keys` ("Esc", "CapsLock", "A", "Mute"; "@N" is slot N).
 *    Actions use the syntax of `k86ctl actions` ("B", "LCtrl+C", "Mute", "macro:3:toggle",
 *    "conn:BT2", "sys:WinLock", "Disabled", ...). Macros use the syntax of `k86ctl macro`.
 *  - Layers (profiles) and macro / pattern indexes are 0-based.
 *  - Strings written into caller buffers are NUL-terminated; a buffer that is too small is
 *    an error.
 *  - A k86_keyboard must only be used by one thread at a time. Writes to the keyboard take up
 *    to ~2 s while its firmware commits them to flash; the call blocks for that time.
 *  - Opening /dev/hidraw* needs the udev rule installed with k86ctl (or root).
 */
#ifndef K86_H
#define K86_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define K86_API __attribute__((visibility("default")))
#else
#define K86_API
#endif

/* Version of this API; bumped when functions or structures change incompatibly. */
#define K86_API_VERSION 1

#define K86_SLOTS 128  /* key slots in a key map / per-key colour table */
#define K86_MACROS 50  /* macro slots */

typedef struct k86_keyboard k86_keyboard;
typedef uint32_t k86_rgb; /* 0xRRGGBB */

typedef enum { K86_CHIP_YC3121 = 0, K86_CHIP_YC3123 = 1 } k86_chip;
typedef enum { K86_OS_WINDOWS = 0, K86_OS_MACOS = 1 } k86_os; /* Fn layers (YC3123 has one per OS) */

/* A keyboard found by k86_list(). */
typedef struct {
    char path[64];   /* /dev/hidrawN */
    uint16_t vid, pid;
    char name[128];  /* USB product name */
    char model[64];  /* "K86 (YC3121)", or "" if unknown */
} k86_device;

/* What the opened keyboard supports. */
typedef struct {
    char name[64];      /* "K86 (YC3121)" */
    char firmware[64];  /* "yc3121_sg8967_3m_soc" */
    k86_chip chip;
    uint16_t vid, pid;
    uint32_t firmware_id;
    int layers;         /* key map profiles */
    int max_brightness, max_speed, max_side_speed;
    int has_side_light;
    int screen_width, screen_height, screen_slots;
    int sleep_min, sleep_max, deep_sleep_min, deep_sleep_max; /* seconds */
} k86_model;

/* A lighting effect. `mode` is an effect code (see k86_light_mode()); `option` selects the
 * direction / pattern of effects that have one; brightness 0..max_brightness; speed
 * 0..max_speed (max_side_speed for the side light); `rainbow` = multi-colour. */
typedef struct {
    uint8_t mode, option, brightness, speed;
    k86_rgb color;
    int rainbow;
} k86_light;

typedef struct {
    uint16_t bluetooth, wireless24;           /* seconds until sleep */
    uint16_t deep_bluetooth, deep_wireless24; /* seconds until deep sleep */
} k86_sleep;

typedef void (*k86_progress_fn)(double fraction, void* user);

/* ---------------------------------------------------------------- library and connection */
K86_API const char* k86_version(void);    /* "0.2.1" */
K86_API const char* k86_last_error(void); /* "" if the last call succeeded */

/* Fills up to `max` entries; returns the number of keyboards found (may exceed max), -1 on error. */
K86_API int k86_list(k86_device* out, int max);
/* Opens `path` (NULL = first K86 found). `force` skips the firmware id check. NULL on error. */
K86_API k86_keyboard* k86_open(const char* path, int force);
/* An in-memory keyboard that answers like the real one: for tests and trying things out. */
K86_API k86_keyboard* k86_open_simulated(k86_chip chip);
K86_API void k86_close(k86_keyboard* kb);
K86_API int k86_get_model(k86_keyboard* kb, k86_model* out);

/* ------------------------------------------------------------------------------ settings */
K86_API int k86_get_profile(k86_keyboard* kb, int* layer);
K86_API int k86_set_profile(k86_keyboard* kb, int layer);
K86_API int k86_get_debounce(k86_keyboard* kb, int* ms);
K86_API int k86_set_debounce(k86_keyboard* kb, int ms);
K86_API int k86_get_sleep(k86_keyboard* kb, k86_sleep* out);
K86_API int k86_set_sleep(k86_keyboard* kb, const k86_sleep* times);
/* Option flags by name, as in `k86ctl options` ("winLock", "wasdSwap", ...). */
K86_API int k86_get_options(k86_keyboard* kb, char* names, size_t size); /* "name=value\n"... */
K86_API int k86_get_option(k86_keyboard* kb, const char* name, int* value);
K86_API int k86_set_option(k86_keyboard* kb, const char* name, int value);

/* ------------------------------------------------------------------------------ lighting */
/* `side` = 0 for the backlight, 1 for the side light. */
K86_API int k86_light_mode(k86_keyboard* kb, const char* name, int side); /* "wave" -> code, -1 */
K86_API const char* k86_light_mode_name(k86_keyboard* kb, int code, int side); /* NULL if unknown */
K86_API int k86_light_modes(k86_keyboard* kb, int side, char* names, size_t size); /* "off\nstatic\n..." */
K86_API int k86_get_light(k86_keyboard* kb, k86_light* out);
K86_API int k86_set_light(k86_keyboard* kb, const k86_light* light);
K86_API int k86_get_side_light(k86_keyboard* kb, k86_light* out);
K86_API int k86_set_side_light(k86_keyboard* kb, const k86_light* light);

/* -------------------------------------------------------------------- keys and Fn layers */
K86_API int k86_key_slot(k86_keyboard* kb, const char* key); /* slot, or -1 */
K86_API int k86_get_key(k86_keyboard* kb, int layer, const char* key, char* action, size_t size);
K86_API int k86_set_key(k86_keyboard* kb, int layer, const char* key, const char* action);
K86_API int k86_reset_keys(k86_keyboard* kb, int layer); /* factory layout of one profile */
K86_API int k86_get_fn_key(k86_keyboard* kb, int layer, k86_os os, const char* key, char* action, size_t size);
K86_API int k86_set_fn_key(k86_keyboard* kb, int layer, k86_os os, const char* key, const char* action);
K86_API int k86_reset_fn_keys(k86_keyboard* kb, int layer, k86_os os);
/* Human readable name of an action: "conn:BT1" -> "Bluetooth 1", "macro:2:toggle" -> "Macro 3 (toggle)". */
K86_API int k86_action_label(const char* action, char* label, size_t size);

/* -------------------------------------------------------------------------------- macros */
K86_API int k86_get_macro(k86_keyboard* kb, int index, char* events, size_t size, int* repeat);
K86_API int k86_set_macro(k86_keyboard* kb, int index, const char* events, int repeat);
K86_API int k86_clear_macro(k86_keyboard* kb, int index);

/* ---------------------------------------------------------------------- per-key colours */
/* Colours of the "custom" effect, indexed by key slot. `pattern` is 0 on the YC3121, 0..4 on
 * the YC3123. */
K86_API int k86_get_key_colors(k86_keyboard* kb, int pattern, k86_rgb colors[K86_SLOTS]);
K86_API int k86_set_key_colors(k86_keyboard* kb, int pattern, const k86_rgb colors[K86_SLOTS]);
K86_API int k86_set_key_color(k86_keyboard* kb, int pattern, const char* key, k86_rgb color);

/* ------------------------------------------------------------------------- screen (LCD) */
/* PNG / JPEG / BMP / GIF / TGA; a still image goes to `slot` (1..screen_slots), a GIF replaces
 * the animation (`delay_ms` per frame, 0 = from the file). `progress` may be NULL. */
K86_API int k86_screen_upload(k86_keyboard* kb, const char* image_path, int slot, int delay_ms,
                              k86_progress_fn progress, void* user);
K86_API int k86_screen_sync_clock(k86_keyboard* kb);
/* Sends CPU / memory / disk / network stats once; call about every second. */
K86_API int k86_screen_send_system_info(k86_keyboard* kb);
K86_API int k86_screen_erase(k86_keyboard* kb); /* erases the stored images (~55 s, runs on the keyboard) */

/* Erases key maps, macros and lighting. */
K86_API int k86_factory_reset(k86_keyboard* kb);

#ifdef __cplusplus
}
#endif

#endif /* K86_H */
