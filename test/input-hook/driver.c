#include "driver.h"

static void pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
}

static void pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
}

static void pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    ((struct driver_counts*)data)->motion++;
}

static void pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    ((struct driver_counts*)data)->button++;
}

static void pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void pointer_frame(void* data, struct wl_pointer* pointer)
{
    ((struct driver_counts*)data)->frame++;
}

static void pointer_axis_source(void* data, struct wl_pointer* pointer, uint32_t source)
{
}

static void pointer_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis)
{
}

static void pointer_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
    .enter         = pointer_enter,
    .leave         = pointer_leave,
    .motion        = pointer_motion,
    .button        = pointer_button,
    .axis          = pointer_axis,
    .frame         = pointer_frame,
    .axis_source   = pointer_axis_source,
    .axis_stop     = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap(void* data, struct wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size)
{
}

static void keyboard_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys)
{
}

static void keyboard_leave(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface)
{
}

static void keyboard_key(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    ((struct driver_counts*)data)->key++;
}

static void keyboard_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group)
{
    ((struct driver_counts*)data)->modifiers++;
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* keyboard, int32_t rate, int32_t delay)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = keyboard_keymap,
    .enter       = keyboard_enter,
    .leave       = keyboard_leave,
    .key         = keyboard_key,
    .modifiers   = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

__attribute__((visibility("default"))) void driver_listen(struct wl_pointer* pointer, struct wl_keyboard* keyboard, struct driver_counts* counts)
{
    wl_pointer_add_listener(pointer, &pointer_listener, counts);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, counts);
}

#define COUNT(field) ((struct driver_counts*)data)->field++

static void touch_down(void* data, struct wl_touch* touch, uint32_t serial, uint32_t time, struct wl_surface* surface, int32_t id, wl_fixed_t x,
                       wl_fixed_t y)
{
    COUNT(touch_down);
}

static void touch_up(void* data, struct wl_touch* touch, uint32_t serial, uint32_t time, int32_t id)
{
    COUNT(touch_up);
}

static void touch_frame(void* data, struct wl_touch* touch)
{
    COUNT(touch_frame);
}

static const struct wl_touch_listener touch_listener = {
    .down  = touch_down,
    .up    = touch_up,
    .frame = touch_frame,
};

static void relative_motion(void* data, struct zwp_relative_pointer_v1* relative, uint32_t hi, uint32_t lo, wl_fixed_t dx, wl_fixed_t dy,
                            wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    COUNT(relative_motion);
}

static const struct zwp_relative_pointer_v1_listener relative_listener = {
    .relative_motion = relative_motion,
};

static void swipe_begin(void* data, struct zwp_pointer_gesture_swipe_v1* swipe, uint32_t serial, uint32_t time, struct wl_surface* surface,
                        uint32_t fingers)
{
    COUNT(swipe_begin);
}

static void swipe_end(void* data, struct zwp_pointer_gesture_swipe_v1* swipe, uint32_t serial, uint32_t time, int32_t cancelled)
{
    COUNT(swipe_end);
}

static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
    .begin = swipe_begin,
    .end   = swipe_end,
};

static void pinch_update(void* data, struct zwp_pointer_gesture_pinch_v1* pinch, uint32_t time, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale,
                         wl_fixed_t rotation)
{
    COUNT(pinch_update);
}

static void pinch_end(void* data, struct zwp_pointer_gesture_pinch_v1* pinch, uint32_t serial, uint32_t time, int32_t cancelled)
{
    COUNT(pinch_end);
}

static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
    .update = pinch_update,
    .end    = pinch_end,
};

static void hold_begin(void* data, struct zwp_pointer_gesture_hold_v1* hold, uint32_t serial, uint32_t time, struct wl_surface* surface,
                       uint32_t fingers)
{
    COUNT(hold_begin);
}

static void hold_end(void* data, struct zwp_pointer_gesture_hold_v1* hold, uint32_t serial, uint32_t time, int32_t cancelled)
{
    COUNT(hold_end);
}

static const struct zwp_pointer_gesture_hold_v1_listener hold_listener = {
    .begin = hold_begin,
    .end   = hold_end,
};

static void tool_proximity_in(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, struct zwp_tablet_v2* tablet, struct wl_surface* surface)
{
    COUNT(tool_proximity_in);
}

static void tool_down(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial)
{
    COUNT(tool_down);
}

static void tool_up(void* data, struct zwp_tablet_tool_v2* tool)
{
    COUNT(tool_up);
}

static void tool_motion(void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y)
{
    COUNT(tool_motion);
}

static void tool_button(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, uint32_t button, uint32_t state)
{
    COUNT(tool_button);
}

static void tool_frame(void* data, struct zwp_tablet_tool_v2* tool, uint32_t time)
{
    COUNT(tool_frame);
}

static const struct zwp_tablet_tool_v2_listener tool_listener = {
    .proximity_in = tool_proximity_in,
    .down         = tool_down,
    .up           = tool_up,
    .motion       = tool_motion,
    .button       = tool_button,
    .frame        = tool_frame,
};

static void pad_button(void* data, struct zwp_tablet_pad_v2* pad, uint32_t time, uint32_t button, uint32_t state)
{
    COUNT(pad_button);
}

static const struct zwp_tablet_pad_v2_listener pad_listener = {
    .button = pad_button,
};

static void ring_angle(void* data, struct zwp_tablet_pad_ring_v2* ring, wl_fixed_t degrees)
{
    COUNT(ring_angle);
}

static void ring_stop(void* data, struct zwp_tablet_pad_ring_v2* ring)
{
    COUNT(ring_stop);
}

static const struct zwp_tablet_pad_ring_v2_listener ring_listener = {
    .angle = ring_angle,
    .stop  = ring_stop,
};

static void strip_position(void* data, struct zwp_tablet_pad_strip_v2* strip, uint32_t position)
{
    COUNT(strip_position);
}

static const struct zwp_tablet_pad_strip_v2_listener strip_listener = {
    .position = strip_position,
};

#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
static void dial_delta(void* data, struct zwp_tablet_pad_dial_v2* dial, int32_t value120)
{
    COUNT(dial_delta);
}

static const struct zwp_tablet_pad_dial_v2_listener dial_listener = {
    .delta = dial_delta,
};
#endif

static void text_commit(void* data, struct zwp_text_input_v3* text, const char* committed)
{
    COUNT(text_commit);
}

static void text_done(void* data, struct zwp_text_input_v3* text, uint32_t serial)
{
    COUNT(text_done);
}

static const struct zwp_text_input_v3_listener text_listener = {
    .commit_string = text_commit,
    .done          = text_done,
};

__attribute__((visibility("default"))) void driver_listen_inputs(const struct driver_inputs* inputs, struct driver_counts* counts)
{
    wl_touch_add_listener(inputs->touch, &touch_listener, counts);
    zwp_relative_pointer_v1_add_listener(inputs->relative, &relative_listener, counts);
    zwp_pointer_gesture_swipe_v1_add_listener(inputs->swipe, &swipe_listener, counts);
    zwp_pointer_gesture_pinch_v1_add_listener(inputs->pinch, &pinch_listener, counts);
    zwp_pointer_gesture_hold_v1_add_listener(inputs->hold, &hold_listener, counts);
    zwp_tablet_tool_v2_add_listener(inputs->tool, &tool_listener, counts);
    zwp_tablet_pad_v2_add_listener(inputs->pad, &pad_listener, counts);
    zwp_tablet_pad_ring_v2_add_listener(inputs->ring, &ring_listener, counts);
    zwp_tablet_pad_strip_v2_add_listener(inputs->strip, &strip_listener, counts);
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    zwp_tablet_pad_dial_v2_add_listener((struct zwp_tablet_pad_dial_v2*)inputs->dial, &dial_listener, counts);
#endif
    zwp_text_input_v3_add_listener(inputs->text, &text_listener, counts);
}
