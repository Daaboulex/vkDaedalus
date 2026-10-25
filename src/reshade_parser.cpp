#include "reshade_parser.hpp"

#include <algorithm>

#include "logger.hpp"
#include "config_serializer.hpp"
#include "shader_cache.hpp"
#include "reshade_compile_defines.hpp"

namespace vkBasalt
{
    namespace
    {
        template<typename T>
        auto findAnnotation(const T& annotations, const std::string& name)
        {
            return std::find_if(annotations.begin(), annotations.end(),
                [&name](const auto& a) { return a.name == name; });
        }

        template<typename T>
        bool hasAnnotation(const T& annotations, const std::string& name)
        {
            return findAnnotation(annotations, name) != annotations.end();
        }

        template<typename T>
        float getAnnotationFloat(const T& annotation)
        {
            return annotation.type.is_floating_point()
                ? annotation.value.as_float[0]
                : static_cast<float>(annotation.value.as_int[0]);
        }

        template<typename T>
        int getAnnotationInt(const T& annotation)
        {
            return annotation.type.is_integral()
                ? annotation.value.as_int[0]
                : static_cast<int>(annotation.value.as_float[0]);
        }

        std::vector<std::string> parseNullSeparatedString(const std::string& str)
        {
            std::vector<std::string> items;
            size_t start = 0;

            for (size_t i = 0; i <= str.size(); i++)
            {
                bool atEnd = (i == str.size() || str[i] == '\0');
                if (!atEnd)
                    continue;

                if (i > start)
                    items.push_back(str.substr(start, i - start));
                start = i + 1;
            }

            return items;
        }

        void applyFloatRange(FloatParam& p, const auto& annotations)
        {
            auto minIt = findAnnotation(annotations, "ui_min");
            auto maxIt = findAnnotation(annotations, "ui_max");

            if (minIt != annotations.end())
                p.minValue = getAnnotationFloat(*minIt);
            if (maxIt != annotations.end())
                p.maxValue = getAnnotationFloat(*maxIt);
        }

        void applyIntRange(IntParam& p, const auto& annotations)
        {
            auto minIt = findAnnotation(annotations, "ui_min");
            auto maxIt = findAnnotation(annotations, "ui_max");

            if (minIt != annotations.end())
                p.minValue = getAnnotationInt(*minIt);
            if (maxIt != annotations.end())
                p.maxValue = getAnnotationInt(*maxIt);
        }

        std::unique_ptr<EffectParam> typedParameter(const reshadefx::uniform& spec, const std::string& effectName, Config* pConfig)
        {
            auto populateFloatVector = [&](FloatVecParam& p, uint32_t componentCount) {
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_float[c];
                    p.value[c] = pConfig->getInstanceOption<float>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = getAnnotationFloat(*minIt);
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = getAnnotationFloat(*maxIt);
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            auto populateIntVector = [&](IntVecParam& p, uint32_t componentCount) {
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_int[c];
                    p.value[c] = pConfig->getInstanceOption<int32_t>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = getAnnotationInt(*minIt);
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = getAnnotationInt(*maxIt);
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            auto populateUintVector = [&](UintVecParam& p, uint32_t componentCount) {
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_uint[c];
                    p.value[c] = pConfig->getInstanceOption<uint32_t>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = static_cast<uint32_t>(getAnnotationInt(*minIt));
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = static_cast<uint32_t>(getAnnotationInt(*maxIt));
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            if (spec.type.is_floating_point() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<FloatVecParam>();
                populateFloatVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_floating_point() && spec.type.rows == 1)
            {
                auto p = std::make_unique<FloatParam>();
                p->defaultValue = spec.initializer_value.as_float[0];
                p->value = pConfig->getInstanceOption<float>(effectName, spec.name, p->defaultValue);
                applyFloatRange(*p, spec.annotations);

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                return p;
            }
            else if (spec.type.is_boolean())
            {
                auto p = std::make_unique<BoolParam>();
                p->defaultValue = (spec.initializer_value.as_uint[0] != 0);
                p->value = pConfig->getInstanceOption<bool>(effectName, spec.name, p->defaultValue);
                return p;
            }
            else if (spec.type.is_integral() && spec.type.is_signed() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<IntVecParam>();
                populateIntVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_integral() && spec.type.is_signed() && spec.type.rows == 1)
            {
                auto p = std::make_unique<IntParam>();
                p->defaultValue = spec.initializer_value.as_int[0];
                p->value = pConfig->getInstanceOption<int32_t>(effectName, spec.name, p->defaultValue);
                applyIntRange(*p, spec.annotations);

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                auto itemsIt = findAnnotation(spec.annotations, "ui_items");
                if (itemsIt != spec.annotations.end())
                    p->items = parseNullSeparatedString(itemsIt->value.string_data);

                return p;
            }
            else if (spec.type.is_integral() && !spec.type.is_signed() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<UintVecParam>();
                populateUintVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_integral() && !spec.type.is_signed() && spec.type.rows == 1)
            {
                auto p = std::make_unique<UintParam>();
                p->defaultValue = spec.initializer_value.as_uint[0];
                p->value = pConfig->getInstanceOption<uint32_t>(effectName, spec.name, p->defaultValue);

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                if (minIt != spec.annotations.end())
                    p->minValue = static_cast<uint32_t>(getAnnotationInt(*minIt));
                if (maxIt != spec.annotations.end())
                    p->maxValue = static_cast<uint32_t>(getAnnotationInt(*maxIt));

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                return p;
            }

            return nullptr;
        }

        std::string annotationText(const reshadefx::uniform& spec, const std::string& name)
        {
            auto it = findAnnotation(spec.annotations, name);
            return it != spec.annotations.end() ? it->value.string_data : "";
        }

        bool annotationSet(const reshadefx::uniform& spec, const std::string& name)
        {
            auto it = findAnnotation(spec.annotations, name);
            return it != spec.annotations.end() && getAnnotationInt(*it) != 0;
        }

        std::unique_ptr<EffectParam> convertSpecConstant(const reshadefx::uniform& spec, const std::string& effectName, Config* pConfig)
        {
            std::unique_ptr<EffectParam> p = typedParameter(spec, effectName, pConfig);
            if (!p)
                return nullptr;
            p->effectName     = effectName;
            p->name           = spec.name;
            p->label          = hasAnnotation(spec.annotations, "ui_label") ? annotationText(spec, "ui_label") : spec.name;
            p->tooltip        = annotationText(spec, "ui_tooltip");
            p->uiType         = annotationText(spec, "ui_type");
            p->category       = annotationText(spec, "ui_category");
            p->categoryClosed = annotationSet(spec, "ui_category_closed");
            p->units          = annotationText(spec, "ui_units");
            p->text           = annotationText(spec, "ui_text");
            p->hidden         = annotationSet(spec, "hidden") || (p->uiType == "radio" && !hasAnnotation(spec.annotations, "ui_items"));
            return p;
        }

        bool shouldSkipSpecConstant(const reshadefx::uniform& spec)
        {
            if (spec.name.empty())
                return true;
            if (hasAnnotation(spec.annotations, "source"))
                return true;
            return false;
        }
    }

    std::vector<std::pair<std::string, std::string>> parameterReadDefines(const std::vector<PreprocessorDefinition>& defs)
    {
        return reshadeCompileDefines({1920, 1080}, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, defs);
    }

    std::vector<std::unique_ptr<EffectParam>> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig,
        const std::vector<PreprocessorDefinition>& defs)
    {
        std::vector<std::unique_ptr<EffectParam>> params;

        try
        {

        ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
        auto compiled = getOrCompileReshadeEffect(effectPath, parameterReadDefines(defs), smConfig.discoveredShaderPaths);
        if (!compiled->ok())
        {
            Logger::err("reshade_parser: " + effectPath + ": " + compiled->error);
            return params;
        }

        const reshadefx::effect_module& module = compiled->module;

        // ReShade's SPIR-V codegen emits a vector spec constant as one same-named scalar per component
        for (size_t i = 0; i < module.spec_constants.size(); i++)
        {
            const auto& spec = module.spec_constants[i];

            if (shouldSkipSpecConstant(spec))
                continue;

            size_t componentCount = 1;
            while (i + componentCount < module.spec_constants.size() &&
                   module.spec_constants[i + componentCount].name == spec.name)
            {
                componentCount++;
            }

            if (componentCount >= 2 && componentCount <= 4)
            {
                auto labelIt = findAnnotation(spec.annotations, "ui_label");
                std::string label = (labelIt != spec.annotations.end()) ? labelIt->value.string_data : spec.name;

                auto tooltipIt = findAnnotation(spec.annotations, "ui_tooltip");
                std::string tooltip = (tooltipIt != spec.annotations.end()) ? tooltipIt->value.string_data : "";

                auto typeIt = findAnnotation(spec.annotations, "ui_type");
                std::string uiType = (typeIt != spec.annotations.end()) ? typeIt->value.string_data : "";

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                auto stepIt = findAnnotation(spec.annotations, "ui_step");

                if (spec.type.is_floating_point())
                {
                    auto p = std::make_unique<FloatVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_float[0];
                        p->value[c] = pConfig->getInstanceOption<float>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = getAnnotationFloat(*minIt);
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = getAnnotationFloat(*maxIt);
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }
                else if (spec.type.is_integral() && spec.type.is_signed())
                {
                    auto p = std::make_unique<IntVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_int[0];
                        p->value[c] = pConfig->getInstanceOption<int32_t>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = getAnnotationInt(*minIt);
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = getAnnotationInt(*maxIt);
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }
                else if (spec.type.is_integral() && !spec.type.is_signed())
                {
                    auto p = std::make_unique<UintVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_uint[0];
                        p->value[c] = pConfig->getInstanceOption<uint32_t>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = static_cast<uint32_t>(getAnnotationInt(*minIt));
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = static_cast<uint32_t>(getAnnotationInt(*maxIt));
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }

                i += componentCount - 1;
            }
            else
            {
                auto param = convertSpecConstant(spec, effectName, pConfig);
                if (param)
                    params.push_back(std::move(param));
            }
        }

        for (const auto& uniform : module.uniforms)
        {
            if (shouldSkipSpecConstant(uniform))
                continue;

            auto param = convertSpecConstant(uniform, effectName, pConfig);
            if (param)
                params.push_back(std::move(param));
        }

        }
        catch (const std::exception& e)
        {
            Logger::err("parseReshadeEffect exception for " + effectName + ": " + e.what());
        }
        catch (...)
        {
            Logger::err("parseReshadeEffect unknown exception for " + effectName);
        }

        return params;
    }

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath)
    {
        ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
        return testShaderCompilation(effectName, effectPath, smConfig.discoveredShaderPaths);
    }

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths)
    {
        ShaderTestResult result;
        result.effectName = effectName;
        result.filePath = effectPath;

        try
        {
            auto compiled = getOrCompileReshadeEffect(effectPath, parameterReadDefines({}), includePaths);
            result.success = compiled->ok();
            result.errorMessage = compiled->ok() ? compiled->warnings : compiled->error;
            result.usesDepth = compiled->usesDepth;
            result.usesMinPrecision = compiled->usesMinPrecision;
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.errorMessage = "Exception: " + std::string(e.what());
        }
        catch (...)
        {
            result.success = false;
            result.errorMessage = "Unknown exception during compilation";
        }

        return result;
    }

    std::vector<PreprocessorDefinition> extractPreprocessorDefinitions(
        const std::string& effectName,
        const std::string& effectPath)
    {
        std::vector<PreprocessorDefinition> defs;

        try
        {
            ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
            auto compiled = getOrCompileReshadeEffect(effectPath, parameterReadDefines({}), smConfig.discoveredShaderPaths);
            if (!compiled->ok())
            {
                Logger::err("extractPreprocessorDefinitions: " + effectPath + ": " + compiled->error);
                return defs;
            }

            for (const auto& [name, value] : compiled->usedMacros)
            {
                const DefinitionScope scope = reshadeDefinitionScope(name);
                if (scope == DefinitionScope::Runtime)
                    continue;

                PreprocessorDefinition def;
                def.name = name;
                def.effectName = scope == DefinitionScope::Global ? "" : effectName;
                def.defaultValue = value.empty() ? "1" : value;
                def.value = def.defaultValue;
                defs.push_back(def);
            }

            if (!defs.empty())
            {
                Logger::debug("extractPreprocessorDefinitions: found " + std::to_string(defs.size()) +
                    " user macros in " + effectName);
                for (const auto& def : defs)
                    Logger::debug("  " + def.name + " = " + def.defaultValue);
            }
        }
        catch (const std::exception& e)
        {
            Logger::err("extractPreprocessorDefinitions exception for " + effectName + ": " + e.what());
        }
        catch (...)
        {
            Logger::err("extractPreprocessorDefinitions unknown exception for " + effectName);
        }

        return defs;
    }

}
