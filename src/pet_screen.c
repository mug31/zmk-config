/*
 * Right-half OLED (SSD1306 128x32, mounted vertically).
 * Everything is drawn in portrait coordinates (32 wide x 128 tall) and rotated
 * 90 degrees onto the landscape panel, same direction as zmk-nice-oled.
 *
 *   rows   0-17  Bluetooth (link to the left half) + battery icon + percent
 *   row      18  dotted separator
 *   rows 100-121 Luna, who sits / walks / runs with how fast you type on this half
 *   row     123  ground
 *
 * Luna pixel art by HellSingCoder (QMK, GPL-2.0), taken from mctechnology17/zmk-nice-oled.
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
#define LUNA_TOP 100
#define LUNA_ROWS 22
#define GROUND_ROW 123

/* Same convention as zmk-nice-oled's default (non-inverted) mode. */
#define PX_ON lv_color_black()
#define PX_OFF lv_color_white()

/* sit1, sit2, walk1, walk2, run1, run2 */
static const uint32_t luna[6][LUNA_ROWS] = {
    {/* dog_sit1 */
        0x00004100, 0x0000a280, 0x00015c80, 0x00010180,
        0x00010080, 0x00021140, 0x00020040, 0x00020140,
        0x00040040, 0x00040740, 0x00080280, 0x00d00080,
        0x01200080, 0x02400080, 0x02400080, 0x02000100,
        0x02000700, 0x01008d00, 0x00c0cd00, 0x0041cd00,
        0x0030cb00, 0x000fb800,
    },
    {/* dog_sit2 */
        0x00004100, 0x0000a280, 0x00015c80, 0x00010180,
        0x00010080, 0x00021140, 0x00020040, 0x00020140,
        0x00040040, 0x00040740, 0x00080280, 0x01900280,
        0x02a00080, 0x04400080, 0x04400080, 0x06000100,
        0x02000700, 0x01008d00, 0x00c0cd00, 0x0041cd00,
        0x0030cb00, 0x000fb800,
    },
    {/* dog_walk1 */
        0x00000000, 0x00000000, 0x00001040, 0x000028a0,
        0x00f05720, 0x01084060, 0x02044020, 0x047f8450,
        0x04808010, 0x04000050, 0x05000010, 0x030001d0,
        0x010000a0, 0x01000040, 0x01000040, 0x01040040,
        0x010f0080, 0x010b8300, 0x009a8d00, 0x009a4d00,
        0x00964b00, 0x00703800,
    },
    {/* dog_walk2 */
        0x00000000, 0x00000000, 0x00000000, 0x00001040,
        0x000028a0, 0x00e05720, 0x01104060, 0x02084020,
        0x047f8450, 0x04808010, 0x04000050, 0x05000010,
        0x050001d0, 0x030000a0, 0x01000040, 0x01000040,
        0x010c0080, 0x01138100, 0x01296200, 0x01255200,
        0x01436900, 0x01c00700,
    },
    {/* dog_run1 */
        0x00000000, 0x00000000, 0x00001c40, 0x038012a0,
        0x04401990, 0x08401010, 0x08806008, 0x08ff8114,
        0x08000004, 0x04000014, 0x03800004, 0x008000f4,
        0x00800048, 0x01800030, 0x028000c0, 0x03840180,
        0x020b0240, 0x041cc720, 0x05e024e0, 0x06001200,
        0x00000900, 0x00000700,
    },
    {/* dog_run2 */
        0x00000000, 0x00000000, 0x00000000, 0x00003880,
        0x0e002540, 0x12003320, 0x12002010, 0x1201c008,
        0x11fe0114, 0x10000004, 0x08000014, 0x04800004,
        0x038000f4, 0x00800048, 0x00800030, 0x00800040,
        0x00410080, 0x0021c100, 0x00114700, 0x00124d00,
        0x00094b00, 0x00073000,
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

static void draw_luna(int frame) {
    for (int r = 0; r < LUNA_ROWS; r++) {
        for (int x = 0; x < 32; x++) {
            px(x, LUNA_TOP + r, luna[frame][r] & (1u << (31 - x)));
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

/* typing activity: every press on this half adds 100, each tick keeps 85% */
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

#define TICK_MS 150
#define WALK_AT 30
#define RUN_AT 350

static void pet_tick(lv_timer_t *timer) {
    static int activity, ticks, frame = -1;

    activity = activity * 85 / 100 + atomic_set(&presses, 0) * 100;

    /* sit: flip every 6 ticks, walk: every 2, run: every tick */
    int base = activity < WALK_AT ? 0 : activity < RUN_AT ? 2 : 4;
    int every = base == 0 ? 6 : base == 2 ? 2 : 1;
    int next = base + ((ticks++ / every) & 1);
    if (next != frame) {
        frame = next;
        draw_luna(frame);
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
    draw_luna(0);
    draw_status();
    lv_timer_create(pet_tick, TICK_MS, NULL);

    pet_battery_init();
    pet_link_init();
    return screen;
}
