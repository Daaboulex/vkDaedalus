#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdio.h>

typedef int (*GrabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time);
typedef int (*UngrabPointer)(Display*, Time);

int XGrabPointer(Display* display, Window window, Bool ownerEvents, unsigned int eventMask, int pointerMode, int keyboardMode, Window confineTo, Cursor cursor,
                 Time time)
{
    const GrabPointer next = (GrabPointer)dlsym(RTLD_NEXT, "XGrabPointer");
    fprintf(stderr, "grab-interposer: XGrabPointer passed on\n");
    return next ? next(display, window, ownerEvents, eventMask, pointerMode, keyboardMode, confineTo, cursor, time) : GrabNotViewable;
}

int XUngrabPointer(Display* display, Time time)
{
    const UngrabPointer next = (UngrabPointer)dlsym(RTLD_NEXT, "XUngrabPointer");
    fprintf(stderr, "grab-interposer: XUngrabPointer passed on\n");
    return next ? next(display, time) : 0;
}
