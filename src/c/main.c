#include <pebble.h>
#include "logging.h"

static Window *s_window;
static Layer *s_ticks_layer;
static Layer *s_hands_layer;

static struct tm s_tick_time;

static void prv_hands_layer_update_proc(Layer *this, GContext *ctx) {
    logf();
    GRect bounds = layer_get_bounds(this);
    GPoint center = grect_center_point(&bounds);
    center.x -= 1;
    center.y -= 1;

    graphics_context_set_stroke_width(ctx, 3);
    graphics_context_set_stroke_color(ctx, GColorWhite);

    int32_t angle = TRIG_MAX_ANGLE * ((((s_tick_time.tm_hour % 12) * 6) + (s_tick_time.tm_min / 10))) / (12 * 6);
    GPoint point = gpoint_from_polar(grect_crop(bounds, 15), GOvalScaleModeFitCircle, angle);
    graphics_draw_line(ctx, center, point);

    angle = s_tick_time.tm_min * TRIG_MAX_ANGLE / 60;
    point = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
    graphics_draw_line(ctx, center, point);

#ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorRed);
#endif
    graphics_context_set_stroke_width(ctx, 1);

    angle = s_tick_time.tm_sec * TRIG_MAX_ANGLE / 60;
    point = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
    graphics_draw_line(ctx, center, point);

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, center, 6);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, center, 3);
}

static void prv_ticks_layer_update_proc(Layer *this, GContext *ctx) {
    logf();
    GRect bounds = grect_crop(layer_get_bounds(this), PBL_IF_RECT_ELSE(-15, 0));
    GRect crop1 = grect_crop(bounds, 25);
    GRect crop2 = grect_crop(bounds, 12);

    graphics_context_set_stroke_width(ctx, 2);
#ifdef PBL_BW
    graphics_context_set_stroke_color(ctx, GColorWhite);
#endif

    for (int i = 0; i < 12; i++) {
        int32_t angle = TRIG_MAX_ANGLE * i / 12;
        GPoint p1 = gpoint_from_polar(i % 6 == 0 ? crop2 : bounds, GOvalScaleModeFitCircle, angle);
        GPoint p2 = gpoint_from_polar(crop1, GOvalScaleModeFitCircle, angle);

#ifdef PBL_COLOR
        graphics_context_set_stroke_color(ctx, i % 3 == 0 ? GColorLightGray : GColorDarkGray);
#endif

        if (i == 0) {
            graphics_draw_line(ctx, GPoint(p1.x - 4, p1.y), GPoint(p2.x - 4, p2.y));
            graphics_draw_line(ctx, GPoint(p1.x + 4, p1.y), GPoint(p2.x + 4, p2.y));
        } else {
            graphics_draw_line(ctx, p1, p2);
        }
    }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    logf();
    memcpy(&s_tick_time, tick_time, sizeof(struct tm));
    layer_mark_dirty(s_hands_layer);
}

static void prv_window_load(Window *window) {
    logf();
    Layer *root_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root_layer);

    s_ticks_layer = layer_create(bounds);
    layer_set_update_proc(s_ticks_layer, prv_ticks_layer_update_proc);
    layer_add_child(root_layer, s_ticks_layer);

    s_hands_layer = layer_create(grect_crop(bounds, PBL_IF_RECT_ELSE(12, 27)));
    layer_set_update_proc(s_hands_layer, prv_hands_layer_update_proc);
    layer_add_child(root_layer, s_hands_layer);

    time_t now = time(NULL);
    prv_tick_handler(localtime(&now), SECOND_UNIT);
    tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);

    window_set_background_color(window, GColorBlack);
}

static void prv_window_unload(Window *window) {
    logf();
    tick_timer_service_unsubscribe();

    layer_destroy(s_hands_layer);
    layer_destroy(s_ticks_layer);
}

static void prv_init(void) {
    logf();
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = prv_window_load,
        .unload = prv_window_unload
    });
    window_stack_push(s_window, true);
}

static void prv_deinit(void) {
    logf();
    window_destroy(s_window);
}

int main(void) {
    logf();
    prv_init();
    app_event_loop();
    prv_deinit();
}
