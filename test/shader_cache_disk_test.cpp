#include "shader_cache.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::vector<fs::path> entries(const fs::path& dir)
{
    std::vector<fs::path> found;
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(dir, ec))
        if (item.path().extension() == ".vkbfx")
            found.push_back(item.path());
    return found;
}

static std::string readFile(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

static void writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

static std::shared_ptr<const vkBasalt::CompiledReshadeEffect> compile(const char* fx)
{
    const auto compiled = vkBasalt::getOrCompileReshadeEffect(fx, {}, {});
    return compiled && compiled->ok() ? compiled : nullptr;
}

static std::string fillEffect(const char* red)
{
    return std::string("float4 FillPS(float4 pos : SV_Position) : SV_Target { return float4(") + red + ", 0.0, 0.0, 1.0); }\n"
           "void FillVS(uint id : SV_VertexID, out float4 pos : SV_Position)\n"
           "{ pos = float4(id == 2 ? 3.0 : -1.0, id == 1 ? -3.0 : 1.0, 0.0, 1.0); }\n"
           "technique Fill { pass { VertexShader = FillVS; PixelShader = FillPS; } }\n";
}

static bool includes(const vkBasalt::CompiledReshadeEffect& effect, const fs::path& header)
{
    for (const auto& [path, hash] : effect.includedFiles)
        if (path == header.string())
            return true;
    return false;
}

static int fail(const std::string& why)
{
    std::printf("%s\n", why.c_str());
    return 1;
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--compile-only")
        return compile(argv[2]) ? 0 : 1;
    if (argc != 2)
        return fail("usage: shader-cache-disk-test <fixture.fx>");

    const std::string tempEntry  = "/cache/vkdaedalus/key.vkbfx";
    const std::string firstTemp  = vkBasalt::cacheEntryTempPath(tempEntry);
    const std::string secondTemp = vkBasalt::cacheEntryTempPath(tempEntry);
    if (firstTemp == secondTemp)
        return fail("two writes of one cache entry share the temp file " + firstTemp + ", so two threads writing one key tear it");
    if (fs::path(firstTemp).parent_path() != fs::path(tempEntry).parent_path() || fs::path(firstTemp).extension() != ".tmp")
        return fail("the temp file " + firstTemp + " is not a .tmp file beside its entry, so the rename over the entry is not atomic");

    const char* root = std::getenv("XDG_CACHE_HOME");
    if (!root || !*root)
        return fail("XDG_CACHE_HOME must name a scratch directory for this test");
    fs::remove_all(root);
    const fs::path cache = fs::path(root) / "vkdaedalus";
    const char*    fx    = argv[1];

    const auto first = compile(fx);
    if (!first)
        return fail(std::string("the fixture does not compile: ") + fx);
    const std::vector<fs::path> written = entries(cache);
    if (written.size() != 1)
        return fail("one compile must leave one cache entry, found " + std::to_string(written.size()));
    const fs::path    entry = written[0];
    const std::string good  = readFile(entry);

    vkBasalt::clearShaderCache();
    if (!entries(cache).empty())
        return fail("clearing the shader cache left entries on disk");
    const auto recompiled = compile(fx);
    if (!recompiled || recompiled == first)
        return fail("after clearing the shader cache the effect was served from memory instead of compiled again");
    if (recompiled->entryPointSpirv != first->entryPointSpirv || readFile(entry) != good)
        return fail("a compile after clearing the cache must give the same SPIR-V and write the same entry");

    vkBasalt::clearShaderCache();
    std::string damaged = good;
    damaged[damaged.size() - 8] ^= 0x5A;
    writeFile(entry, damaged);
    const auto afterDamage = compile(fx);
    if (!afterDamage || afterDamage->entryPointSpirv != first->entryPointSpirv)
        return fail("an entry with one byte changed on disk was trusted: its SPIR-V reached the caller");
    if (readFile(entry) != good)
        return fail("the damaged entry was not replaced by a fresh compile");

    const fs::path otherBuild = fs::path(root) / "other-build";
    writeFile(otherBuild, readFile("/proc/self/exe") + '\n');
    fs::permissions(otherBuild, fs::perms::owner_all);
    const pid_t child = fork();
    if (child == 0)
    {
        execl(otherBuild.c_str(), otherBuild.c_str(), "--compile-only", fx, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return fail("a second build of this program could not compile the fixture");
    if (entries(cache).size() != 2)
        return fail("a different build read this build's entry instead of compiling its own: found "
                    + std::to_string(entries(cache).size()) + " entries, want 2");

    const fs::path packs = fs::path(root) / "packs";
    fs::create_directories(packs / "fx");
    fs::create_directories(packs / "early");
    fs::create_directories(packs / "late");
    writeFile(packs / "late" / "Shared.fxh", "#define SHARED_RED 0.25\n");
    writeFile(packs / "fx" / "shadowed.fx", "#include \"Shared.fxh\"\n" + fillEffect("SHARED_RED"));
    writeFile(packs / "fx" / "optional.fx",
              "#if exists \"Optional.fxh\"\n#include \"Optional.fxh\"\n#else\n#define OPTIONAL_RED 0.25\n#endif\n" + fillEffect("OPTIONAL_RED"));
    const std::vector<std::string> search = {(packs / "early").string(), (packs / "late").string()};
    const std::string              shadowedFx = (packs / "fx" / "shadowed.fx").string();
    const std::string              optionalFx = (packs / "fx" / "optional.fx").string();

    const auto beforeShadow = vkBasalt::getOrCompileReshadeEffect(shadowedFx, {}, search);
    if (!beforeShadow->ok() || !includes(*beforeShadow, packs / "late" / "Shared.fxh"))
        return fail("the shadowing fixture must first compile against the later header: " + beforeShadow->error);
    writeFile(packs / "early" / "Shared.fxh", "#define SHARED_RED 0.75\n");
    const auto afterShadow = vkBasalt::getOrCompileReshadeEffect(shadowedFx, {}, search);
    if (!afterShadow->ok() || !includes(*afterShadow, packs / "early" / "Shared.fxh"))
        return fail("a header added earlier in the search order was ignored: the entry built against the later one was reused");

    const auto withoutOptional = vkBasalt::getOrCompileReshadeEffect(optionalFx, {}, search);
    if (!withoutOptional->ok() || includes(*withoutOptional, packs / "fx" / "Optional.fxh"))
        return fail("the exists fixture must first compile without its optional header: " + withoutOptional->error);
    writeFile(packs / "fx" / "Optional.fxh", "#define OPTIONAL_RED 0.5\n");
    const auto withOptional = vkBasalt::getOrCompileReshadeEffect(optionalFx, {}, search);
    if (!withOptional->ok() || !includes(*withOptional, packs / "fx" / "Optional.fxh"))
        return fail("a header that #if exists found missing appeared, and the entry built without it was reused");

    std::printf("the shader cache refuses damaged entries, clears on request, keeps each build's entries apart and follows headers that appear\n");
    return 0;
}
