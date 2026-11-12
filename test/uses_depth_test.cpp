#include "shader_cache.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3 || argc % 2 == 0)
    {
        std::printf("usage: uses-depth-test <fixture.fx> <depth|none per technique, comma separated>...\n");
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
        std::vector<bool>  wanted;
        std::istringstream words(argv[i + 1]);
        for (std::string word; std::getline(words, word, ',');)
            wanted.push_back(word == "depth");
        if (wanted.size() != compiled->module.techniques.size())
        {
            std::printf("FAIL: %s has %zu techniques, the test names %zu\n", argv[i], compiled->module.techniques.size(), wanted.size());
            failures++;
            continue;
        }
        bool any = false;
        for (size_t t = 0; t < wanted.size(); t++)
        {
            any = any || wanted[t];
            if (compiled->techniqueReadsDepth(t) != wanted[t])
            {
                std::printf("FAIL: %s technique %s reads depth: %s, want %s\n", argv[i], compiled->module.techniques[t].name.c_str(),
                            compiled->techniqueReadsDepth(t) ? "yes" : "no", wanted[t] ? "yes" : "no");
                failures++;
            }
        }
        if (compiled->usesDepth() != any)
        {
            std::printf("FAIL: %s reads depth: %s, want %s\n", argv[i], compiled->usesDepth() ? "yes" : "no", any ? "yes" : "no");
            failures++;
        }
    }
    if (failures != 0)
        return 1;
    std::printf("every fixture's depth use was seen as it is, technique by technique\n");
    return 0;
}
