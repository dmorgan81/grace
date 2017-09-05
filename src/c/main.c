#include <pebble.h>
#include <ctype.h>
#include <pebble-events/pebble-events.h>
#include <pebble-hourly-vibes/hourly-vibes.h>
#include <pebble-connection-vibes/connection-vibes.h>
#include "enamel.h"
#include "weather.h"
#include "logging.h"

static GFont s_font;

static Window *s_window;
static Layer *s_ticks_layer;
static TextLayer *s_date_layer;
#ifndef PBL_PLATFORM_APLITE
static Layer *s_quiet_time_layer;
#endif
static TextLayer *s_weather_layer;
static Layer *s_hands_layer;

static struct tm s_tick_time;
static bool s_connected;

static EventHandle s_connection_event_handle;
static EventHandle s_tick_timer_event_handle;
static EventHandle s_settings_received_event_handle;
static EventHandle s_weather_event_handle;

#ifndef PBL_PLATFORM_APLITE
static void prv_quiet_time_layer_update_proc(Layer *this, GContext *ctx) {
    logf();
    if (!quiet_time_is_active()) return;

    graphics_context_set_text_color(ctx, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);
    graphics_draw_text(ctx, "QT", s_font, layer_get_bounds(this), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}
#endif

static void prv_hands_layer_update_proc(Layer *this, GContext *ctx) {
    logf();
    GRect bounds = layer_get_bounds(this);
    GPoint center = grect_center_point(&bounds);
    center.x -= 1;
    center.y -= 1;

    graphics_context_set_stroke_width(ctx, 3);
    graphics_context_set_stroke_color(ctx, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);

    int32_t angle = TRIG_MAX_ANGLE * ((((s_tick_time.tm_hour % 12) * 6) + (s_tick_time.tm_min / 10))) / (12 * 6);
    GPoint point = gpoint_from_polar(grect_crop(bounds, 15), GOvalScaleModeFitCircle, angle);
    graphics_draw_line(ctx, center, point);

    angle = s_tick_time.tm_min * TRIG_MAX_ANGLE / 60;
    point = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
    graphics_draw_line(ctx, center, point);

    if (enamel_get_SHOW_SECOND_HAND()) {
#ifdef PBL_COLOR
        graphics_context_set_stroke_color(ctx, GColorRed);
#endif
        graphics_context_set_stroke_width(ctx, 1);

        angle = s_tick_time.tm_sec * TRIG_MAX_ANGLE / 60;
        point = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
        graphics_draw_line(ctx, center, point);
    }

    graphics_context_set_fill_color(ctx, enamel_get_INVERT_COLORS() ? GColorWhite : GColorBlack);
    graphics_fill_circle(ctx, center, 6);

    graphics_context_set_fill_color(ctx, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);
    graphics_fill_circle(ctx, center, 3);

    if (!s_connected) {
        graphics_context_set_fill_color(ctx, enamel_get_INVERT_COLORS() ? GColorWhite : GColorBlack);
        graphics_fill_circle(ctx, center, 2);
    }
}

static void prv_ticks_layer_update_proc(Layer *this, GContext *ctx) {
    logf();
    GRect bounds = grect_crop(layer_get_bounds(this), PBL_IF_RECT_ELSE(-15, 0));
    GRect crop1 = grect_crop(bounds, 25);
    GRect crop2 = grect_crop(bounds, 12);

    graphics_context_set_stroke_width(ctx, 2);
#ifdef PBL_BW
    graphics_context_set_stroke_color(ctx, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);
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

static void prv_app_connection_handler(bool connected) {
    logf();
    s_connected = connected;
    layer_mark_dirty(s_hands_layer);
}

static inline void strupp(char *s) {
    while ((*s++ = (char) toupper((int) *s)));
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    logf();
    if (units_changed & DAY_UNIT) {
        static char s[8];
        strftime(s, sizeof(s), "%a %d", tick_time);
        strupp(s);
        text_layer_set_text(s_date_layer, s);
    }

    memcpy(&s_tick_time, tick_time, sizeof(struct tm));
#ifdef DEMO
    s_tick_time.tm_hour = 8;
    s_tick_time.tm_min = 0;
    s_tick_time.tm_sec = 20;
#endif
    layer_mark_dirty(s_hands_layer);
}

static void prv_weather_handler(GenericWeatherInfo *info, GenericWeatherStatus status, void *context) {
    logf();
    static char s[8];
    if (status == GenericWeatherStatusAvailable) {
        int unit = atoi(enamel_get_WEATHER_UNIT());
        snprintf(s, sizeof(s), "%d°", unit == 1 ? info->temp_f : info->temp_c);
        text_layer_set_text(s_weather_layer, s);
    } else if (status != GenericWeatherStatusPending) {
        text_layer_set_text(s_weather_layer, "--");
    } else {
        text_layer_set_text(s_weather_layer, "??");
    }
}

static void prv_settings_received_handler(void *context) {
    logf();
    hourly_vibes_set_enabled(enamel_get_HOURLY_VIBE());
    connection_vibes_set_state(atoi(enamel_get_CONNECTION_VIBE()));
#ifdef PBL_HEALTH
    connection_vibes_enable_health(enamel_get_ENABLE_HEALTH());
    hourly_vibes_enable_health(enamel_get_ENABLE_HEALTH());
#endif

    text_layer_set_text_color(s_date_layer, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);
    text_layer_set_text_color(s_weather_layer, enamel_get_INVERT_COLORS() ? GColorBlack : GColorWhite);
    window_set_background_color(s_window, enamel_get_INVERT_COLORS() ? GColorWhite: GColorBlack);

    layer_set_hidden(text_layer_get_layer(s_weather_layer), !enamel_get_WEATHER_ENABLED());
    if (enamel_get_WEATHER_ENABLED() && s_weather_event_handle == NULL) {
        prv_weather_handler(weather_peek(), weather_status_peek(), NULL);
        s_weather_event_handle = events_weather_subscribe(prv_weather_handler, NULL);
    } else if (!enamel_get_WEATHER_ENABLED() && s_weather_event_handle != NULL) {
        events_weather_unsubscribe(s_weather_event_handle);
    }

    if (s_tick_timer_event_handle)
        events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);

    time_t now = time(NULL);
    prv_tick_handler(localtime(&now), DAY_UNIT);
    s_tick_timer_event_handle = events_tick_timer_service_subscribe(
        enamel_get_SHOW_SECOND_HAND() ? SECOND_UNIT : MINUTE_UNIT, prv_tick_handler);
}

static void prv_window_load(Window *window) {
    logf();
    Layer *root_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root_layer);

    s_ticks_layer = layer_create(bounds);
    layer_set_update_proc(s_ticks_layer, prv_ticks_layer_update_proc);
    layer_add_child(root_layer, s_ticks_layer);

    s_date_layer = text_layer_create(GRect(0, PBL_IF_RECT_ELSE(78, 84), bounds.size.w - PBL_IF_RECT_ELSE(15, 30), bounds.size.h));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_font(s_date_layer, s_font);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentRight);
    layer_add_child(root_layer, text_layer_get_layer(s_date_layer));

#ifndef PBL_PLATFORM_APLITE
    s_quiet_time_layer = layer_create(GRect(PBL_IF_RECT_ELSE(15, 30), PBL_IF_RECT_ELSE(78, 84), 15, 15));
    layer_set_update_proc(s_quiet_time_layer, prv_quiet_time_layer_update_proc);
    layer_add_child(root_layer, s_quiet_time_layer);
#endif

    s_weather_layer = text_layer_create(GRect(0, bounds.size.h - 40, bounds.size.w, 20));
    text_layer_set_background_color(s_weather_layer, GColorClear);
    text_layer_set_font(s_weather_layer, s_font);
    text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
    layer_add_child(root_layer, text_layer_get_layer(s_weather_layer));

    s_hands_layer = layer_create(grect_crop(bounds, PBL_IF_RECT_ELSE(12, 27)));
    layer_set_update_proc(s_hands_layer, prv_hands_layer_update_proc);
    layer_add_child(root_layer, s_hands_layer);

    prv_app_connection_handler(connection_service_peek_pebble_app_connection());
    s_connection_event_handle = events_connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = prv_app_connection_handler
    });

    prv_settings_received_handler(NULL);
    s_settings_received_event_handle = enamel_settings_received_subscribe(prv_settings_received_handler, NULL);
}

static void prv_window_unload(Window *window) {
    logf();
    if (s_weather_event_handle) events_weather_unsubscribe(s_weather_event_handle);
    if (s_tick_timer_event_handle) events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);
    enamel_settings_received_unsubscribe(s_settings_received_event_handle);
    events_connection_service_unsubscribe(s_connection_event_handle);

    layer_destroy(s_hands_layer);
    text_layer_destroy(s_weather_layer);
#ifndef PBL_PLATFORM_APLITE
    layer_destroy(s_quiet_time_layer);
#endif
    text_layer_destroy(s_date_layer);
    layer_destroy(s_ticks_layer);
}

static void prv_init(void) {
    logf();
    setlocale(LC_ALL, "");
    s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_10));

    enamel_init();
    weather_init();
    connection_vibes_init();
    hourly_vibes_init();
    uint32_t const pattern[] = { 100 };
    hourly_vibes_set_pattern((VibePattern) {
        .durations = pattern,
        .num_segments = 1
    });
    events_app_message_open();

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

    connection_vibes_deinit();
    hourly_vibes_deinit();
    weather_deinit();
    enamel_deinit();

    fonts_unload_custom_font(s_font);
}

int main(void) {
    logf();
    prv_init();
    app_event_loop();
    prv_deinit();
}
