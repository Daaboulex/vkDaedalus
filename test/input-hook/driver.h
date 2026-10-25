#pragma once

#include <wayland-client.h>

struct driver_counts
{
    int motion;
    int button;
    int frame;
    int key;
    int modifiers;
};

typedef void (*driver_listen_fn)(struct wl_pointer* pointer, struct wl_keyboard* keyboard, struct driver_counts* counts);
