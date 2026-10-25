#include "shader_cache.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: compile-error-test <broken fixture.fx>\n");
        return 2;
    }

    const auto        compiled = vkBasalt::getOrCompileReshadeEffect(argv[1], {}, {});
    const std::string error    = compiled->error;
    if (compiled->ok())
    {
        std::printf("the broken fixture compiled\n");
        return 1;
    }
    if (error.find("store_typo.fx(6") == std::string::npos)
    {
        std::printf("the error does not point at the faulty line: %s\n", error.c_str());
        return 1;
    }
    if (error.find("needs ReShade") != std::string::npos)
    {
        std::printf("the compiler's error was replaced by a version claim: %s\n", error.c_str());
        return 1;
    }
    std::printf("the compiler's own error reaches the user: %s\n", error.c_str());
    return 0;
}
