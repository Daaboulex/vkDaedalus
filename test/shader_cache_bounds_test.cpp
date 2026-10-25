#include "shader_cache.hpp"

#include <cstdio>
#include <functional>
#include <string>

static reshadefx::uniform uniformAt(const char* name, uint32_t offset, uint32_t size)
{
    reshadefx::uniform u;
    u.name   = name;
    u.offset = offset;
    u.size   = size;
    return u;
}

static vkBasalt::CompiledReshadeEffect consistentEffect()
{
    vkBasalt::CompiledReshadeEffect effect;
    effect.module.entry_points.push_back({"vs", reshadefx::shader_type::vertex});
    effect.module.entry_points.push_back({"ps", reshadefx::shader_type::pixel});
    effect.module.entry_points.push_back({"cs", reshadefx::shader_type::compute});
    effect.entryPointSpirv["vs"] = {0x07230203u};
    effect.entryPointSpirv["ps"] = {0x07230203u};
    effect.entryPointSpirv["cs"] = {0x07230203u};

    reshadefx::texture target;
    target.name = target.unique_name = "Target";
    target.render_target = true;
    target.storage_access = true;
    effect.module.textures.push_back(target);

    reshadefx::sampler sampler;
    sampler.name = sampler.unique_name = "TargetSampler";
    sampler.texture_name = "Target";
    effect.module.samplers.push_back(sampler);

    reshadefx::storage storage;
    storage.name = storage.unique_name = "TargetStorage";
    storage.texture_name = "Target";
    effect.module.storages.push_back(storage);

    reshadefx::pass draw;
    draw.vs_entry_point = "vs";
    draw.ps_entry_point = "ps";
    draw.render_target_names[0] = "Target";
    reshadefx::pass dispatch;
    dispatch.cs_entry_point = "cs";
    reshadefx::technique technique;
    technique.name = "Main";
    technique.passes = {draw, dispatch};
    effect.module.techniques.push_back(technique);

    effect.module.total_uniform_size = 32;
    effect.module.uniforms.push_back(uniformAt("first", 0, 16));
    effect.module.uniforms.push_back(uniformAt("last", 16, 16));
    return effect;
}

static bool refusedOnRead(const char* what, const char* named, const std::function<void(vkBasalt::CompiledReshadeEffect&)>& breakIt)
{
    vkBasalt::CompiledReshadeEffect effect = consistentEffect();
    breakIt(effect);
    const std::string mismatch = vkBasalt::cacheRoundTripMismatch(effect);
    if (mismatch.rfind("deserialize:", 0) == 0 && mismatch.find(named) != std::string::npos)
        return true;
    std::printf("%s must be refused on read naming '%s', got: '%s'\n", what, named, mismatch.c_str());
    return false;
}

int main()
{
    const std::string consistent = vkBasalt::cacheRoundTripMismatch(consistentEffect());
    if (!consistent.empty())
    {
        std::printf("a consistent entry whose uniforms fill the block exactly must round-trip, got: %s\n", consistent.c_str());
        return 1;
    }

    int failures = 0;
    failures += !refusedOnRead("a cached uniform past the end of the uniform block", "beyond",
                               [](auto& e) { e.module.uniforms.push_back(uniformAt("beyond", 32, 4)); });
    failures += !refusedOnRead("a cached uniform whose offset plus size wraps", "wraps",
                               [](auto& e) { e.module.uniforms.push_back(uniformAt("wraps", 0xFFFFFFF0u, 32)); });
    failures += !refusedOnRead("an entry with no technique", "technique", [](auto& e) { e.module.techniques.clear(); });
    failures += !refusedOnRead("an entry with no SPIR-V", "SPIR-V", [](auto& e) { e.entryPointSpirv.clear(); });
    failures += !refusedOnRead("an entry whose only technique has no pass and no SPIR-V", "has no SPIR-V", [](auto& e) {
        e.module.techniques[0].passes.clear();
        e.entryPointSpirv.clear();
    });
    failures += !refusedOnRead("a pass naming an entry point without SPIR-V", "entry point ps",
                               [](auto& e) { e.entryPointSpirv.erase("ps"); });
    failures += !refusedOnRead("a draw pass without a pixel shader", "pass 0",
                               [](auto& e) { e.module.techniques[0].passes[0].ps_entry_point.clear(); });
    failures += !refusedOnRead("a pass rendering to a texture that is not declared", "renders to Missing",
                               [](auto& e) { e.module.techniques[0].passes[0].render_target_names[1] = "Missing"; });
    failures += !refusedOnRead("a sampler of a texture that is not declared", "samples Missing",
                               [](auto& e) { e.module.samplers[0].texture_name = "Missing"; });
    failures += !refusedOnRead("a storage of a texture that is not declared", "writes Missing",
                               [](auto& e) { e.module.storages[0].texture_name = "Missing"; });
    if (failures != 0)
        return 1;

    std::printf("cached entries are refused on read unless every name they use resolves and every uniform fits the block\n");
    return 0;
}
