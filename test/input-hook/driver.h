#pragma once

#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "tablet-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

#include <wayland-client.h>

struct driver_counts
{
    int motion;
    int button;
    int frame;
    int key;
    int modifiers;
    int touch_down;
    int touch_up;
    int touch_frame;
    int relative_motion;
    int swipe_begin;
    int swipe_end;
    int pinch_update;
    int pinch_end;
    int hold_begin;
    int hold_end;
    int tool_proximity_in;
    int tool_motion;
    int tool_down;
    int tool_up;
    int tool_button;
    int tool_frame;
    int pad_button;
    int ring_angle;
    int ring_stop;
    int strip_position;
    int dial_delta;
    int text_commit;
    int text_done;
};

struct driver_inputs
{
    struct wl_touch*                     touch;
    struct zwp_relative_pointer_v1*      relative;
    struct zwp_pointer_gesture_swipe_v1* swipe;
    struct zwp_pointer_gesture_pinch_v1* pinch;
    struct zwp_pointer_gesture_hold_v1*  hold;
    struct zwp_tablet_tool_v2*           tool;
    struct zwp_tablet_pad_v2*            pad;
    struct zwp_tablet_pad_ring_v2*       ring;
    struct zwp_tablet_pad_strip_v2*      strip;
    struct wl_proxy*                     dial;
    struct zwp_text_input_v3*            text;
};

typedef void (*driver_listen_fn)(struct wl_pointer* pointer, struct wl_keyboard* keyboard, struct driver_counts* counts);
typedef void (*driver_listen_inputs_fn)(const struct driver_inputs* inputs, struct driver_counts* counts);
