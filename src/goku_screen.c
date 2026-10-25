/*
 * Right-half OLED (SSD1306 128x32, mounted vertically).
 * Everything is drawn in portrait coordinates (32 wide x 128 tall) and rotated
 * 90 degrees onto the landscape panel, same direction as zmk-nice-oled.
 *
 *   rows   0-17  Bluetooth (link to the left half) + battery icon + percent
 *   row      18  dotted separator
 *   rows 20-127  Goku half face (from ~/Downloads/goku.jpg)
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>

#define PANEL_W 128
#define PANEL_H 32
#define GOKU_TOP 20
#define GOKU_ROWS 108

/* Same convention as zmk-nice-oled's default (non-inverted) mode. */
#define PX_ON lv_color_black()
#define PX_OFF lv_color_white()

static const uint32_t goku[GOKU_ROWS] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00400000,
    0xbfa80000, 0xffe00000, 0xf9cf8000, 0xf0f9c800,
    0xc0f07c00, 0x00403e00, 0x00001f00, 0x00001f00,
    0x00000f80, 0x00000780, 0x00000f80, 0x00000f80,
    0x00001fc0, 0x00001fe0, 0x00001fe0, 0x00001fe0,
    0x00003fe0, 0x00007fe8, 0x00007fe8, 0x0000fff8,
    0x0001fff0, 0x0003fff3, 0x0007ffff, 0x0007fffc,
    0x0007fff8, 0x000fffe0, 0x000ff780, 0x000fc700,
    0x000f8400, 0x000c0001, 0x0000001b, 0x00000efb,
    0x00001fff, 0x00001fff, 0x0000ffff, 0x0003ffff,
    0x281c0fff, 0x28003fff, 0x00003fff, 0x000003ff,
    0x00000fff, 0x007e1fff, 0x03f83fff, 0x03e03fff,
    0x40803fff, 0x40003fff, 0x40003fff, 0xc0007fff,
    0xe0007fff, 0xe0007fff, 0xe0007fff, 0xe0007fff,
    0x00007fff, 0x0000ffff, 0x0000ffff, 0x0000fe7f,
    0x0000feff, 0x0000fcff, 0x0001f1ff, 0x0001e1ff,
    0x0003c1ff, 0x000783ff, 0x000703ff, 0x000e03ff,
    0x000807ff, 0x001007ff, 0x000007ff, 0x00000fff,
    0x00000ffe, 0x00000ffe, 0x00001ffe, 0x00001ffe,
    0x00003ffe, 0x00c03bfc, 0x1f8033fc, 0x0f8073ff,
    0x070073ff, 0x030063ff, 0x0300e3ff, 0x0200e1ff,
    0x0000e1ff, 0x0001c1ff, 0x0001c1ff, 0x0001c0ff,
    0x0001c0ff, 0x0001c07f, 0x0001c03f, 0x0001c03f,
    0x0001c0ff, 0x800181ff, 0xc00007ff, 0x000001ff,
};

/* 3x5 digits, one byte per row, bits 2..0 = left..right */
static const uint8_t digits[11][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    {5, 1, 2, 4, 5}, /* % */
};

/* Bluetooth rune, 5x9, bits 4..0 = left..right */
static const uint8_t bt_icon[9] = {0x04, 0x06, 0x15, 0x0e, 0x04, 0x0e, 0x15, 0x06, 0x04};

static lv_obj_t *canvas;
static lv_color_t cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(PANEL_W, PANEL_H)];

static uint8_t battery_level;
static bool link_connected;

static void px(int x, int y, bool on) {
    if (x < 0 || x >= 32 || y < 0 || y >= 128) {
        return;
    }
    cbuf[x * PANEL_W + (PANEL_W - 1 - y)] = on ? PX_ON : PX_OFF;
}

static void draw_goku(void) {
    for (int r = 0; r < GOKU_ROWS; r++) {
        for (int x = 0; x < 32; x++) {
            px(x, GOKU_TOP + r, goku[r] & (1u << (31 - x)));
        }
    }
}

static void draw_glyph(const uint8_t rows[5], int x0, int y0) {
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 3; c++) {
            px(x0 + c, y0 + r, rows[r] & (4 >> c));
        }
    }
}

static void draw_status(void) {
    for (int y = 0; y < GOKU_TOP; y++) {
        for (int x = 0; x < 32; x++) {
            px(x, y, false);
        }
    }

    /* Bluetooth rune when linked to the left half, a small x when not */
    if (link_connected) {
        for (int r = 0; r < 9; r++) {
            for (int c = 0; c < 5; c++) {
                px(1 + c, 2 + r, bt_icon[r] & (0x10 >> c));
            }
        }
    } else {
        for (int i = 0; i < 5; i++) {
            px(1 + i, 4 + i, true);
            px(5 - i, 4 + i, true);
        }
    }

    /* battery: 16x7 outline with a nub, fill proportional to charge */
    const int bx = 10, by = 3, bw = 16, bh = 7;
    for (int x = bx; x < bx + bw; x++) {
        px(x, by, true);
        px(x, by + bh - 1, true);
    }
    for (int y = by; y < by + bh; y++) {
        px(bx, y, true);
        px(bx + bw - 1, y, true);
    }
    px(bx + bw, by + 2, true);
    px(bx + bw, by + 3, true);
    px(bx + bw, by + 4, true);
    int fill = (battery_level * (bw - 4) + 50) / 100;
    for (int x = 0; x < fill; x++) {
        for (int y = by + 2; y < by + bh - 2; y++) {
            px(bx + 2 + x, y, true);
        }
    }

    /* percent text, right-aligned under the battery */
    char text[5];
    int n = snprintf(text, sizeof(text), "%u%%", battery_level);
    int x = bx + bw + 1 - n * 4;
    for (int i = 0; i < n; i++, x += 4) {
        draw_glyph(digits[text[i] == '%' ? 10 : text[i] - '0'], x, 12);
    }

    for (int x = 2; x < 30; x += 2) {
        px(x, 18, true);
    }

    lv_obj_invalidate(canvas);
}

/* battery */
struct battery_state {
    uint8_t level;
};

static void battery_update_cb(struct battery_state state) {
    battery_level = state.level;
    draw_status();
}

static struct battery_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(goku_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(goku_battery, zmk_battery_state_changed);

/* link to the central (left) half */
struct link_state {
    bool connected;
};

static void link_update_cb(struct link_state state) {
    link_connected = state.connected;
    draw_status();
}

static struct link_state link_get_state(const zmk_event_t *eh) {
    return (struct link_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(goku_link, struct link_state, link_update_cb, link_get_state)
ZMK_SUBSCRIPTION(goku_link, zmk_split_peripheral_status_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, cbuf, PANEL_W, PANEL_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(canvas, PX_OFF, LV_OPA_COVER);

    draw_goku();
    draw_status();

    goku_battery_init();
    goku_link_init();
    return screen;
}
