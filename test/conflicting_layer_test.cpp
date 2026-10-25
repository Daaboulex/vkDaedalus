#include <cstdio>
#include <dlfcn.h>
#include <string>

#include "util.hpp"

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    expect(vkBasalt::conflictingLayerPath().empty(), "with no upstream vkBasalt loaded none is reported");
    void* loaded = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    expect(loaded != nullptr, "the stand-in libvkbasalt.so loads");
    expect(vkBasalt::conflictingLayerPath().empty(), "the miss holds between scans, so drawing a frame walks no library list");
    vkBasalt::rescanForConflictingLayer();
    const std::string found = vkBasalt::conflictingLayerPath();
    expect(found.size() >= 14 && found.compare(found.size() - 14, 14, "libvkbasalt.so") == 0, "a rescan, as opening the overlay runs, finds a vkBasalt loaded after the start");
    if (failures)
        return 1;
    std::printf("conflicting-layer: every check passed\n");
    return 0;
}
