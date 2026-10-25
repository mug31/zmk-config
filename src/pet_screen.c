/*
 * Right-half OLED (SSD1306 128x32, mounted vertically).
 * Everything is drawn in portrait coordinates (32 wide x 128 tall) and rotated
 * 90 degrees onto the landscape panel, same direction as zmk-nice-oled.
 *
 *   rows   0-17  Bluetooth (link to the left half) + battery icon + percent
 *   row      18  dotted separator
 *   rows  86-121 Naruto: ninja stance when idle, Naruto-runs while you type on this half
 *   row     123  ground
 *
 * Naruto frames: fan sprite sheet from github.com/SKJTCodes/Naruto, halved and made 1-bit.
 * Only key presses on the right half count: the peripheral never sees the left half's keys.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/position_state_changed.h>

#define PANEL_W 128
#define PANEL_H 32
#define SPRITE_TOP 86
#define SPRITE_ROWS 36
#define GROUND_ROW 123

/* Same convention as zmk-nice-oled's default (non-inverted) mode. */
#define PX_ON lv_color_black()
#define PX_OFF lv_color_white()

/* idle1, idle2, run1..run6 */
static const uint32_t naruto[8][SPRITE_ROWS] = {
    {/* idle1 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00004000, 0x0013c000, 0x241fe000,
        0x7c9ff000, 0x001fe000, 0x000e3800, 0x004fe000,
        0x011cc800, 0x390ffc00, 0x0807fe00, 0x0563de00,
        0x0ff3ae00, 0x07f7f7c0, 0x0707f3ee, 0x0107f9da,
        0x0007f8fc, 0x000ff03c, 0x001ffc3c, 0x003ffc38,
        0x007ffe20, 0x00feff00, 0x01fe7f80, 0x03f81fc0,
        0x03e007c0, 0x07e007e0, 0x07c007e0, 0x03c003e0,
        0x0f0000e0, 0x1f8000f0, 0x3f0000f8, 0x1c000078,
    },
    {/* idle2 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00024000, 0x000bc000, 0x000de000, 0x4817f000,
        0x086fe000, 0x00063800, 0x09cfe000, 0x201cc800,
        0x1017fc00, 0x0007fe00, 0x0011fe00, 0x02fbee00,
        0x07fbf780, 0x07f3f7e0, 0x01e3f9e8, 0x0083f8d6,
        0x0007f0fc, 0x000ff838, 0x001ffc70, 0x003fbe70,
        0x007fff40, 0x00fe7f80, 0x01fe3fc0, 0x01f80fc0,
        0x03e007e0, 0x03e007e0, 0x03e003e0, 0x03c003e0,
        0x07c000e0, 0x0fc000f0, 0x1f8000f8, 0x0e000078,
    },
    {/* run1 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000004, 0x0000f13c, 0x0038e1be,
        0x002438ff, 0x000231ff, 0x000202ef, 0x00180076,
        0x003fe3ff, 0x03fffeff, 0x03ffffff, 0x01f7ffde,
        0x001fff9e, 0x001fff04, 0x003ffe00, 0x007ff800,
        0x00fffc00, 0x03fffe00, 0x07f97f00, 0x1feffe00,
        0x3f8ffc00, 0x7e0ff000, 0xfc1e4000, 0xf81c0000,
        0xe0180000, 0xc0000000, 0xc0000000, 0x80000000,
    },
    {/* run2 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000004, 0x0000013c, 0x000101be,
        0x000840ff, 0x00084bff, 0x000008ef, 0x00000076,
        0x00277fff, 0x03ffbeff, 0x0dffffff, 0x3ef3ffde,
        0x7c13f39e, 0xfe1ffe04, 0xffbffe00, 0x07ffd000,
        0x07ffe000, 0x03fff000, 0x01fbf800, 0x00f0f800,
        0x0060f800, 0x0000fc00, 0x00007c00, 0x00007000,
        0x00007e00, 0x00007f00, 0x00007e00, 0x00007000,
    },
    {/* run3 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000040,
        0x000013c0, 0x00401be0, 0x00101ff8, 0x00124ff0,
        0x00020ef0, 0x00000760, 0x0073fff0, 0x1ff9eff0,
        0x1ffffff0, 0x1ffffff0, 0x0f5fffe0, 0x01fff040,
        0x01ffc000, 0x07ff0000, 0x077e0000, 0x07ff0000,
        0x07ff0000, 0x05fc0000, 0x1ffc0000, 0x1ff00000,
        0x1ec00000, 0x0e000000, 0x0e000000, 0x04000000,
    },
    {/* run4 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000004, 0x0000013c, 0x000008be,
        0x002000ff, 0x002010ff, 0x0006116f, 0x000001f6,
        0x00007fff, 0x0001feff, 0x003fffff, 0x03ffffde,
        0x03ffff9e, 0x00effe04, 0x003ffe00, 0x007ff800,
        0x00fffc00, 0x03fdfe00, 0x07f97f00, 0x1feffe00,
        0x3f8ffc00, 0x7e0ff000, 0xfc1e4000, 0xf81c0000,
        0xe0180000, 0xc0000000, 0xc0000000, 0x80000000,
    },
    {/* run5 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000004, 0x0001a13c, 0x0000a1be,
        0x001c07ff, 0x000031ff, 0x00073aef, 0x00070076,
        0x000c7fff, 0x01ff7eff, 0x0fffffff, 0x3d6fffde,
        0x7c10df9e, 0xfe1ffe04, 0xffbf7e00, 0x07ffd000,
        0x07fee000, 0x03fff000, 0x01fbf800, 0x00f0f800,
        0x0060f800, 0x0000fc00, 0x00007c00, 0x00007000,
        0x00007e00, 0x00007f00, 0x00007e00, 0x00007000,
    },
    {/* run6 */
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00120040,
        0x001513c0, 0x00409be0, 0x00030ff8, 0x00274ff0,
        0x006006f0, 0x00000760, 0x0073fff0, 0x1ff9eff0,
        0x1ffffff0, 0x1ffffff0, 0x0f5fffe0, 0x01fff040,
        0x01ffc000, 0x07ff0000, 0x073e0000, 0x07ff0000,
        0x07ff0000, 0x07fe0000, 0x1ff00000, 0x1ff00000,
        0x1ec00000, 0x0e000000, 0x0e000000, 0x04000000,
    },
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

static void draw_sprite(int frame) {
    for (int r = 0; r < SPRITE_ROWS; r++) {
        for (int x = 0; x < 32; x++) {
            px(x, SPRITE_TOP + r, naruto[frame][r] & (1u << (31 - x)));
        }
    }
    lv_obj_invalidate(canvas);
}

static void draw_glyph(const uint8_t rows[5], int x0, int y0) {
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 3; c++) {
            px(x0 + c, y0 + r, rows[r] & (4 >> c));
        }
    }
}

static void draw_status(void) {
    for (int y = 0; y < 19; y++) {
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

ZMK_DISPLAY_WIDGET_LISTENER(pet_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(pet_battery, zmk_battery_state_changed);

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

ZMK_DISPLAY_WIDGET_LISTENER(pet_link, struct link_state, link_update_cb, link_get_state)
ZMK_SUBSCRIPTION(pet_link, zmk_split_peripheral_status_changed);

/* typing activity: every press on this half adds 100, each tick keeps 92% (~1 s to settle) */
static atomic_t presses;

static int pet_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev != NULL && ev->state) {
        atomic_inc(&presses);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(pet_keys, pet_key_listener);
ZMK_SUBSCRIPTION(pet_keys, zmk_position_state_changed);

#define TICK_MS 80
#define RUN_AT 30
#define FAST_AT 300

static void pet_tick(lv_timer_t *timer) {
    static int activity, ticks, frame = -1;

    activity = activity * 92 / 100 + atomic_set(&presses, 0) * 100;
    ticks++;

    int next;
    if (activity < RUN_AT) {
        next = (ticks / 5) & 1; /* idle stance, 400 ms per frame */
    } else {
        int every = activity < FAST_AT ? 2 : 1; /* 160 ms or 80 ms per frame */
        next = 2 + (ticks / every) % 6;
    }
    if (next != frame) {
        frame = next;
        draw_sprite(frame);
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, cbuf, PANEL_W, PANEL_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(canvas, PX_OFF, LV_OPA_COVER);

    for (int x = 0; x < 32; x += 2) {
        px(x, GROUND_ROW, true);
    }
    draw_sprite(0);
    draw_status();
    lv_timer_create(pet_tick, TICK_MS, NULL);

    pet_battery_init();
    pet_link_init();
    return screen;
}
