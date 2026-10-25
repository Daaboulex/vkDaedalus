#include "shader_cache.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc < 3 || argc % 2 == 0)
    {
        std::printf("usage: uses-depth-test <fixture.fx> <depth|none>...\n");
        return 2;
    }

    int failures = 0;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const auto compiled = vkBasalt::getOrCompileReshadeEffect(argv[i], {}, {});
        if (!compiled->ok())
        {
            std::printf("FAIL: %s does not compile: %s\n", argv[i], compiled->error.c_str());
            failures++;
            continue;
        }
        const bool wanted = std::strcmp(argv[i + 1], "depth") == 0;
        if (compiled->usesDepth != wanted)
        {
            std::printf("FAIL: %s reads depth: %s, want %s\n", argv[i], compiled->usesDepth ? "yes" : "no", wanted ? "yes" : "no");
            failures++;
        }
    }
    if (failures != 0)
        return 1;
    std::printf("every fixture's depth use was seen as it is\n");
    return 0;
}
