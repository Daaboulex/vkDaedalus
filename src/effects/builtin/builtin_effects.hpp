#ifndef BUILTIN_EFFECTS_HPP_INCLUDED
#define BUILTIN_EFFECTS_HPP_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

#include "../effect.hpp"
#include "../params/effect_param.hpp"
#include "logical_device.hpp"
#include "config.hpp"

namespace vkBasalt
{
    struct ParamDef
    {
        std::string name;
        std::string label;
        ParamType type;
        float defaultFloat = 0.0f;
        float minFloat = 0.0f;
        float maxFloat = 1.0f;
        int defaultInt = 0;
        int minInt = 0;
        int maxInt = 100;
    };

    using EffectFactory = std::function<std::shared_ptr<Effect>(
        LogicalDevice* pLogicalDevice,
        VkFormat format,
        VkExtent2D extent,
        std::vector<VkImage> inputImages,
        std::vector<VkImage> outputImages,
        Config* pConfig)>;

    struct BuiltInEffectDef
    {
        std::string typeName;
        std::string purpose;
        bool usesSrgbFormat;
        std::vector<ParamDef> params;
        EffectFactory factory;
    };

    class BuiltInEffects
    {
    public:
        static const BuiltInEffects& instance();

        bool isBuiltIn(const std::string& typeName) const;

        const BuiltInEffectDef* getDef(const std::string& typeName) const;

        std::vector<std::string> getTypeNames() const;

    private:
        BuiltInEffects();
        std::map<std::string, BuiltInEffectDef> effects;
    };

}

#endif
