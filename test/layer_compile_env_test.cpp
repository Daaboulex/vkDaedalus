#include "shader_cache.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: layer-compile-env-test <fixture.fx>...\n");
        return 2;
    }

    for (int i = 1; i < argc; i++)
    {
        const auto compiled = vkBasalt::getOrCompileReshadeEffect(argv[i], {}, {});
        if (!compiled)
        {
            std::printf("the layer's compile path returned nothing for %s\n", argv[i]);
            return 1;
        }
        if (!compiled->error.empty())
        {
            std::printf("the layer's compile path rejects %s: %s\n", argv[i], compiled->error.c_str());
            return 1;
        }
    }

    std::printf("the layer's compile path accepts all %d fixtures\n", argc - 1);
    return 0;
}
