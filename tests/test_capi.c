/* libk86 C API test against the simulated keyboard (never opens hardware). Uses only the public
 * header and is compiled as C99, so it also checks that k86.h is valid C. */
#include <k86/k86.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: FAIL %s (last error: %s)\n", __FILE__, __LINE__, #cond, \
                    k86_last_error());                                                       \
            ++failures;                                                                      \
        }                                                                                    \
    } while (0)

static double lastProgress = 0;
static void onProgress(double f, void* user) {
    (void)user;
    lastProgress = f;
}

static void testChip(k86_chip chip) {
    char buf[1024];
    int v = -1, repeat = 0;
    k86_model m;
    k86_light l, got;
    k86_sleep s;
    k86_rgb colors[K86_SLOTS];
    k86_keyboard* kb = k86_open_simulated(chip);
    CHECK(kb != NULL);
    if (!kb) return;

    CHECK(k86_get_model(kb, &m) == 0);
    CHECK(m.chip == chip && m.layers == 3 && m.max_brightness == 4);
    CHECK(strstr(m.name, "K86") != NULL);

    /* Keys: same syntax as k86ctl. */
    CHECK(k86_key_slot(kb, "CapsLock") >= 0);
    CHECK(k86_key_slot(kb, "NoSuchKey") == -1 && strstr(k86_last_error(), "unknown key"));
    CHECK(k86_set_key(kb, 0, "CapsLock", "LCtrl+C") == 0);
    CHECK(k86_get_key(kb, 0, "CapsLock", buf, sizeof buf) == 0 && strcmp(buf, "LCtrl+C") == 0);
    CHECK(k86_get_key(kb, 1, "CapsLock", buf, sizeof buf) == 0 && strcmp(buf, "CapsLock") == 0);
    CHECK(k86_set_key(kb, 0, "F1", "macro:2:toggle") == 0);
    CHECK(k86_get_key(kb, 0, "F1", buf, sizeof buf) == 0);
    CHECK(k86_action_label(buf, buf, sizeof buf) == 0 && strcmp(buf, "Macro 3 (toggle)") == 0);
    CHECK(k86_action_label("conn:BT1", buf, sizeof buf) == 0 && strcmp(buf, "Bluetooth 1") == 0);
    CHECK(k86_set_key(kb, 0, "Esc", "NotAnAction") == -1 && k86_last_error()[0] != 0);
    CHECK(k86_set_key(kb, 3, "Esc", "A") == -1 && strstr(k86_last_error(), "layer"));
    CHECK(k86_get_key(kb, 0, "CapsLock", buf, 4) == -1 && strstr(k86_last_error(), "buffer too small"));
    CHECK(k86_reset_keys(kb, 0) == 0);
    CHECK(k86_get_key(kb, 0, "CapsLock", buf, sizeof buf) == 0 && strcmp(buf, "CapsLock") == 0);

    /* Fn layer. */
    CHECK(k86_set_fn_key(kb, 0, K86_OS_WINDOWS, "1", "conn:BT2") == 0);
    CHECK(k86_get_fn_key(kb, 0, K86_OS_WINDOWS, "1", buf, sizeof buf) == 0 && strcmp(buf, "conn:BT2") == 0);
    CHECK(k86_reset_fn_keys(kb, 0, K86_OS_WINDOWS) == 0);
    CHECK(k86_get_fn_key(kb, 0, K86_OS_WINDOWS, "1", buf, sizeof buf) == 0 && strcmp(buf, "conn:BT2") != 0);

    /* Macros. */
    CHECK(k86_set_macro(kb, 4, "+LCtrl/5 C/20 -LCtrl/10", 2) == 0);
    CHECK(k86_get_macro(kb, 4, buf, sizeof buf, &repeat) == 0);
    CHECK(strcmp(buf, "+LCtrl/5 +C/20 -C/20 -LCtrl/10") == 0 || strcmp(buf, "+LCtrl/5 C/20 -LCtrl/10") == 0);
    CHECK(repeat == 2);
    CHECK(k86_clear_macro(kb, 4) == 0);
    CHECK(k86_get_macro(kb, 4, buf, sizeof buf, NULL) == 0 && buf[0] == 0);
    CHECK(k86_set_macro(kb, 50, "A", 1) == -1);

    /* Lighting. */
    CHECK(k86_light_modes(kb, 0, buf, sizeof buf) == 0 && strstr(buf, "wave\n"));
    memset(&l, 0, sizeof l);
    l.mode = (uint8_t)k86_light_mode(kb, "wave", 0);
    l.option = 1;
    l.brightness = 3;
    l.speed = 2;
    l.rainbow = 1;
    CHECK(k86_set_light(kb, &l) == 0);
    CHECK(k86_get_light(kb, &got) == 0 && got.mode == l.mode && got.option == 1 && got.brightness == 3 &&
          got.speed == 2 && got.rainbow);
    CHECK(strcmp(k86_light_mode_name(kb, got.mode, 0), "wave") == 0);
    l.brightness = 9;
    CHECK(k86_set_light(kb, &l) == -1 && strstr(k86_last_error(), "brightness"));
    CHECK(k86_light_mode(kb, "nope", 0) == -1);
    memset(&l, 0, sizeof l);
    l.mode = (uint8_t)k86_light_mode(kb, "static", 1);
    l.brightness = 4;
    l.color = 0xFF00FF;
    CHECK(k86_set_side_light(kb, &l) == 0);
    CHECK(k86_get_side_light(kb, &got) == 0 && got.mode == l.mode && got.color == 0xFF00FF);

    /* Per-key colours. */
    CHECK(k86_set_key_color(kb, 0, "Esc", 0x123456) == 0);
    CHECK(k86_get_key_colors(kb, 0, colors) == 0 && colors[k86_key_slot(kb, "Esc")] == 0x123456);
    colors[k86_key_slot(kb, "W")] = 0x00FF00;
    CHECK(k86_set_key_colors(kb, 0, colors) == 0);
    CHECK(k86_get_key_colors(kb, 0, colors) == 0 && colors[k86_key_slot(kb, "W")] == 0x00FF00);
    CHECK(k86_get_key_colors(kb, chip == K86_CHIP_YC3123 ? 4 : 1, colors) == (chip == K86_CHIP_YC3123 ? 0 : -1));

    /* Settings. */
    CHECK(k86_set_profile(kb, 2) == 0 && k86_get_profile(kb, &v) == 0 && v == 2);
    CHECK(k86_set_debounce(kb, 7) == 0 && k86_get_debounce(kb, &v) == 0 && v == 7);
    CHECK(k86_get_sleep(kb, &s) == 0);
    s.bluetooth = 300;
    CHECK(k86_set_sleep(kb, &s) == 0 && k86_get_sleep(kb, &s) == 0 && s.bluetooth == 300);
    s.deep_bluetooth = 1;
    CHECK(k86_set_sleep(kb, &s) == -1 && strstr(k86_last_error(), "sleep"));
    CHECK(k86_get_options(kb, buf, sizeof buf) == 0 && strstr(buf, "wasdSwap=0\n"));
    CHECK(k86_set_option(kb, "wasdSwap", 1) == 0 && k86_get_option(kb, "wasdSwap", &v) == 0 && v == 1);
    CHECK(k86_set_option(kb, "noSuchOption", 1) == -1);

    /* Screen commands are accepted by the simulator. */
    CHECK(k86_screen_sync_clock(kb) == 0);
    CHECK(k86_screen_send_system_info(kb) == 0);
    CHECK(k86_screen_upload(kb, "/nonexistent.png", 1, 0, onProgress, NULL) == -1);

    /* Factory reset restores the defaults. */
    CHECK(k86_set_key(kb, 1, "A", "B") == 0);
    CHECK(k86_factory_reset(kb) == 0);
    CHECK(k86_get_key(kb, 1, "A", buf, sizeof buf) == 0 && strcmp(buf, "A") == 0);
    CHECK(k86_last_error()[0] == 0);

    k86_close(kb);
}

int main(void) {
    k86_device devices[4];
    printf("libk86 %s\n", k86_version());
    CHECK(k86_version()[0] != 0);
    CHECK(k86_list(devices, 4) >= 0); /* lists only; nothing is opened */
    CHECK(k86_get_model(NULL, NULL) == -1 && k86_last_error()[0] != 0);
    CHECK(k86_open_simulated((k86_chip)7) == NULL);
    testChip(K86_CHIP_YC3121);
    testChip(K86_CHIP_YC3123);
    (void)lastProgress;
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("C API: all checks passed\n");
    return 0;
}
