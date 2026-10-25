#include <cstdio>
#include <cstdlib>
#include <new>

#include "logger.hpp"

static size_t allocations = 0;

void* operator new(size_t size)
{
    allocations++;
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, size_t) noexcept
{
    std::free(p);
}

int main()
{
    unsetenv("VKDAEDALUS_LOG_LEVEL");
    vkBasalt::Logger::info("the logger is set up before the count starts");
    const size_t before = allocations;
    for (int i = 0; i < 100; i++)
        vkBasalt::Logger::trace("the layer submitted a frame, a line longer than a short string");
    const size_t spent = allocations - before;
    if (spent != 0)
    {
        std::printf("FAIL: 100 trace lines with trace off allocated %zu times; a line nobody records must cost no allocation\n", spent);
        return 1;
    }
    std::printf("logger-cost: trace lines with trace off allocate nothing\n");
    return 0;
}
