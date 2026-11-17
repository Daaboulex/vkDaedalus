#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const char* ownerOf(void* address)
{
    Dl_info info;
    if (address == NULL || dladdr(address, &info) == 0 || info.dli_fname == NULL)
        return "none";
    const char* const slash = strrchr(info.dli_fname, '/');
    return slash != NULL ? slash + 1 : info.dli_fname;
}

__attribute__((constructor)) static void printTable(void)
{
    static const char* const names[] = {"XGrabPointer", "XUngrabPointer", "XCloseDisplay", "XQueryPointer"};
    void* const              x11     = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        printf("dlsym-table: %s RTLD_NEXT %s\n", names[i], ownerOf(dlsym(RTLD_NEXT, names[i])));
        printf("dlsym-table: %s libX11 %s\n", names[i], ownerOf(x11 != NULL ? dlsym(x11, names[i]) : NULL));
        printf("dlsym-table: %s RTLD_DEFAULT %s\n", names[i], ownerOf(dlsym(RTLD_DEFAULT, names[i])));
    }
    fflush(stdout);
}
