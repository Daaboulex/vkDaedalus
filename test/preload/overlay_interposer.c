#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <link.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* (*Lookup)(void*, const char*);
typedef int (*GrabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time);
typedef int (*UngrabPointer)(Display*, Time);
typedef int (*CloseDisplay)(Display*);

static Lookup glibcLookup(void)
{
    static _Atomic(Lookup) lookup;
    Lookup found = atomic_load(&lookup);
    if (found == NULL)
    {
        found = (Lookup)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
        if (found == NULL)
        {
            fprintf(stderr, "overlay-interposer: glibc gives no dlsym@GLIBC_2.34\n");
            abort();
        }
        atomic_store(&lookup, found);
    }
    return found;
}

static void passedOn(atomic_flag* said, const char* name)
{
    if (!atomic_flag_test_and_set(said))
        fprintf(stderr, "overlay-interposer: %s passed on\n", name);
}

static int grabPointer(Display* display, Window window, Bool ownerEvents, unsigned int eventMask, int pointerMode, int keyboardMode, Window confineTo,
                       Cursor cursor, Time time)
{
    static atomic_flag said = ATOMIC_FLAG_INIT;
    const GrabPointer  next = (GrabPointer)glibcLookup()(RTLD_NEXT, "XGrabPointer");
    passedOn(&said, "XGrabPointer");
    return next != NULL ? next(display, window, ownerEvents, eventMask, pointerMode, keyboardMode, confineTo, cursor, time) : GrabNotViewable;
}

static int ungrabPointer(Display* display, Time time)
{
    static atomic_flag  said = ATOMIC_FLAG_INIT;
    const UngrabPointer next = (UngrabPointer)glibcLookup()(RTLD_NEXT, "XUngrabPointer");
    passedOn(&said, "XUngrabPointer");
    return next != NULL ? next(display, time) : 0;
}

static int closeDisplay(Display* display)
{
    static atomic_flag said = ATOMIC_FLAG_INIT;
    const CloseDisplay next = (CloseDisplay)glibcLookup()(RTLD_NEXT, "XCloseDisplay");
    passedOn(&said, "XCloseDisplay");
    return next != NULL ? next(display) : 0;
}

int XGrabPointer(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time) __attribute__((alias("grabPointer")));
int XUngrabPointer(Display*, Time) __attribute__((alias("ungrabPointer")));
int XCloseDisplay(Display*) __attribute__((alias("closeDisplay")));

static void* definedAfter(const void* caller, const char* name)
{
    Dl_info          info;
    struct link_map* from = NULL;
    if (dladdr1(caller, &info, (void**)&from, RTLD_DL_LINKMAP) == 0 || from == NULL)
        return NULL;
    for (struct link_map* object = from->l_next; object != NULL; object = object->l_next)
    {
        void* const handle = object->l_name[0] != '\0' ? dlopen(object->l_name, RTLD_NOW | RTLD_NOLOAD) : NULL;
        if (handle == NULL)
            continue;
        void* const found = glibcLookup()(handle, name);
        dlclose(handle);
        Dl_info          where;
        struct link_map* owner = NULL;
        if (found != NULL && dladdr1(found, &where, (void**)&owner, RTLD_DL_LINKMAP) != 0 && owner == object)
            return found;
    }
    return NULL;
}

void* dlsym(void* handle, const char* name)
{
    if (handle == RTLD_NEXT)
        return definedAfter(__builtin_return_address(0), name);
    if (strcmp(name, "XGrabPointer") == 0)
        return (void*)grabPointer;
    if (strcmp(name, "XUngrabPointer") == 0)
        return (void*)ungrabPointer;
    if (strcmp(name, "XCloseDisplay") == 0)
        return (void*)closeDisplay;
    return glibcLookup()(handle, name);
}
