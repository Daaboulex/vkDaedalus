#include "effect_registry.hpp"
#include "effect_selection.hpp"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <set>
#include <sstream>

#include "reshade_compile_defines.hpp"
#include "reshade_parser.hpp"
#include "config_serializer.hpp"
#include "builtin/builtin_effects.hpp"
#include "logger.hpp"

namespace vkBasalt
{

    namespace
    {
        uint32_t rowGpuFromConfig(const std::string& name, Config* pConfig)
        {
            std::string    problem;
            const uint32_t gpu = rowGpuNumber(pConfig->getInstanceOption<int32_t>(name, "gpu", 0), problem);
            if (!problem.empty())
                Logger::warn(name + ".gpu: " + problem + ", so " + name + " runs on the game's GPU");
            return gpu;
        }

        std::unique_ptr<FloatParam> makeFloatParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            float defaultVal,
            float minVal,
            float maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<FloatParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<float>(effectName, name, pConfig->getOption<float>(name, defaultVal));
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        std::unique_ptr<IntParam> makeIntParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            int defaultVal,
            int minVal,
            int maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<IntParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<int32_t>(effectName, name, pConfig->getOption<int32_t>(name, defaultVal));
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        std::string searchDirsForEffect(const std::string& name,
                                        const std::vector<std::string>& dirs)
        {
            for (const auto& dir : dirs)
            {
                std::string path = dir + "/" + name + ".fx";
                if (std::filesystem::exists(path))
                    return path;

                path = dir + "/" + name;
                if (std::filesystem::exists(path))
                    return path;
            }
            return "";
        }

        std::string findEffectPath(const std::string& name, Config* pConfig)
        {
            const std::string saved = pConfig->getOption<std::string>(name, "");
            if (!saved.empty() && std::filesystem::exists(saved))
                return saved;
            const auto instead = [&](const std::string& found) {
                if (!saved.empty() && !found.empty())
                    Logger::warn(name + ": its saved file " + saved + " is gone, so " + found + " is used");
                return found;
            };
            std::string path;

            std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath", "");
            if (!includePath.empty())
            {
                std::vector<std::string> includeDirs;
                std::stringstream ss(includePath);
                std::string dir;
                while (std::getline(ss, dir, ':'))
                {
                    if (!dir.empty())
                        includeDirs.push_back(dir);
                }
                path = searchDirsForEffect(name, includeDirs);
                if (!path.empty())
                    return instead(path);
            }

            ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();
            path = searchDirsForEffect(name, shaderMgrConfig.discoveredShaderPaths);
            if (!path.empty())
                return instead(path);

            return "";
        }
    }

    bool EffectRegistry::isBuiltInEffect(const std::string& name)
    {
        return BuiltInEffects::instance().isBuiltIn(name);
    }

    std::string EffectRegistry::effectFilePathFor(const std::string& effectType) const
    {
        return isBuiltInEffect(effectType) ? std::string() : findEffectPath(effectType, pConfig);
    }

    void EffectRegistry::initialize(Config* pConfig)
    {
        std::lock_guard<std::mutex> lock(mutex);
        this->pConfig = pConfig;
        effects.clear();
        globalDefs.clear();
        explicitGlobals.clear();
        globalUsers.clear();
        for (const auto& [name, value] : pConfig->getGlobalDefinitions())
        {
            if (const std::string problem = globalDefinitionProblem(name, value); !problem.empty())
            {
                Logger::warn("the global definition @" + name + " is ignored: " + problem);
                continue;
            }
            globalDefs.push_back({name, value, "", ""});
            explicitGlobals.insert(name);
        }

        std::vector<std::string> effectNames = pConfig->getOption<std::vector<std::string>>("effects");
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects");

        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        for (const auto& name : effectNames)
        {
            std::string storedValue = pConfig->getOption<std::string>(name, "");

            if (!storedValue.empty() && isBuiltInEffect(storedValue))
            {
                initBuiltInEffect(name, storedValue);
            }
            else if (isBuiltInEffect(name))
            {
                initBuiltInEffect(name, name);
            }
            else
            {
                std::string effectPath = findEffectPath(name, pConfig);
                if (effectPath.empty())
                {
                    Logger::err("EffectRegistry: could not find effect file for: " + name);
                    continue;
                }
                initReshadeEffect(name, effectPath);
            }

            if (!effects.empty() && disabledSet.count(name))
                effects.back().enabled = false;
        }

        plan.list       = parseGpuList(pConfig->getOption<std::string>("gpus", ""));
        gpuListEdited   = false;
        replanLocked();
        for (const std::string& problem : gpuAssignmentProblems(plan.rows, plan.ownGpus, plan.list))
            Logger::warn(problem + ", so every effect runs on the game's GPU");

        Logger::debug("EffectRegistry: initialized " + std::to_string(effects.size()) + " effects");
        ++parameterStorageRevision;
    }

    std::vector<std::string> EffectRegistry::reshadeEffectFilesOf(Config* pConfig)
    {
        std::vector<std::string> files;
        for (const auto& name : pConfig->getOption<std::vector<std::string>>("effects"))
        {
            const std::string storedValue = pConfig->getOption<std::string>(name, "");
            if ((!storedValue.empty() && isBuiltInEffect(storedValue)) || isBuiltInEffect(name))
                continue;
            if (std::string path = findEffectPath(name, pConfig); !path.empty())
                files.push_back(std::move(path));
        }
        return files;
    }

    void EffectRegistry::initBuiltInEffect(const std::string& instanceName, const std::string& effectType)
    {
        const auto* def = BuiltInEffects::instance().getDef(effectType);
        if (!def)
        {
            Logger::err("Unknown built-in effect type: " + effectType);
            return;
        }

        EffectConfig config;
        config.instanceName = instanceName;
        config.baseType = effectType;
        config.type = EffectType::BuiltIn;
        config.gpu = rowGpuFromConfig(instanceName, pConfig);
        config.enabled = true;

        for (const auto& paramDef : def->params)
        {
            if (paramDef.type == ParamType::Float)
            {
                config.parameters.push_back(
                    makeFloatParam(instanceName, paramDef.name, paramDef.label,
                                   paramDef.defaultFloat, paramDef.minFloat, paramDef.maxFloat, pConfig));
            }
            else if (paramDef.type == ParamType::Int)
            {
                config.parameters.push_back(
                    makeIntParam(instanceName, paramDef.name, paramDef.label,
                                 paramDef.defaultInt, paramDef.minInt, paramDef.maxInt, pConfig));
            }
        }

        effects.push_back(std::move(config));
    }

    void EffectRegistry::initReshadeEffect(const std::string& name, const std::string& path)
    {
        EffectConfig config;
        config.instanceName = name;
        config.filePath = path;
        config.type = EffectType::ReShade;
        config.enabled = true;
        config.gpu = rowGpuFromConfig(name, pConfig);

        std::error_code ec;
        config.fileModTime = std::filesystem::last_write_time(path, ec);

        std::filesystem::path p(path);
        config.baseType = p.stem().string();

        ShaderTestResult testResult = testShaderCompilation(name, path);
        if (!testResult.success)
        {
            config.compileError = testResult.errorMessage;
            Logger::err("EffectRegistry: failed to compile " + name + ": " + testResult.errorMessage);
        }
        else
        {
            config.usesMinPrecision  = testResult.usesMinPrecision;
            config.allowHalfPrecision = pConfig->getInstanceOption<bool>(name, "halfPrecision", false);

            for (auto& def : extractPreprocessorDefinitions(name, path))
            {
                if (def.effectName.empty())
                {
                    addGlobalDefaultLocked(def, name);
                    continue;
                }
                std::string configKey = name + "@" + def.name;
                std::string savedValue = pConfig->getOption<std::string>(configKey, "");
                if (!savedValue.empty())
                {
                    def.value = savedValue;
                    Logger::debug("EffectRegistry: loaded preprocessor def " + configKey + " = " + savedValue);
                }
                config.preprocessorDefs.push_back(def);
            }

            config.parameters       = parseReshadeEffect(name, path, pConfig, {});
            config.parameterDefines = parameterReadDefines({});

            Logger::debug("EffectRegistry: loaded ReShade effect " + name + " with " +
                          std::to_string(config.parameters.size()) + " parameters and " +
                          std::to_string(config.preprocessorDefs.size()) + " preprocessor defs");
        }

        effects.push_back(std::move(config));
    }

    std::vector<const EffectConfig*> EffectRegistry::getEnabledEffects() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<const EffectConfig*> enabled;

        for (const auto& effect : effects)
        {
            if (effect.enabled)
                enabled.push_back(&effect);
        }

        return enabled;
    }

    std::vector<std::unique_ptr<EffectParam>> EffectRegistry::getAllParameters() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::unique_ptr<EffectParam>> params;

        for (const auto& effect : effects)
        {
            for (const auto& p : effect.parameters)
                params.push_back(p->clone());
        }

        return params;
    }

    EffectConfig* EffectRegistry::findEffectLocked(const std::string& effectName)
    {
        for (auto& effect : effects)
        {
            if (effect.instanceName == effectName)
                return &effect;
        }
        return nullptr;
    }

    const EffectConfig* EffectRegistry::findEffectLocked(const std::string& effectName) const
    {
        for (const auto& effect : effects)
        {
            if (effect.instanceName == effectName)
                return &effect;
        }
        return nullptr;
    }

    EffectParam* EffectRegistry::findParamLocked(EffectConfig& effect, const std::string& paramName)
    {
        for (auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    const EffectParam* EffectRegistry::findParamLocked(const EffectConfig& effect, const std::string& paramName) const
    {
        for (const auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    void EffectRegistry::setEffectEnabled(const std::string& effectName, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffectLocked(effectName);
        if (effect)
            effect->enabled = enabled;
    }

    bool EffectRegistry::isEffectEnabled(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffectLocked(effectName);
        return effect ? effect->enabled : false;
    }

    std::map<std::string, bool> EffectRegistry::getEffectEnabledStates() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::map<std::string, bool> states;
        for (const auto& effect : effects)
            states[effect.instanceName] = effect.enabled;
        return states;
    }

    std::vector<std::string> EffectRegistry::getActiveEffects() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::map<std::string, bool> states;
        for (const auto& effect : effects)
            states[effect.instanceName] = effect.enabled;
        return enabledEffectNames(selectedEffects, states);
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, float value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return;

        EffectParam* param = findParamLocked(*effect, paramName);
        if (param && param->getType() == ParamType::Float)
            static_cast<FloatParam*>(param)->value = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return;

        EffectParam* param = findParamLocked(*effect, paramName);
        if (param && param->getType() == ParamType::Int)
            static_cast<IntParam*>(param)->value = value;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return;

        EffectParam* param = findParamLocked(*effect, paramName);
        if (param && param->getType() == ParamType::Bool)
            static_cast<BoolParam*>(param)->value = value;
    }

    EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return nullptr;

        return findParamLocked(*effect, paramName);
    }

    const EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return nullptr;

        return findParamLocked(*effect, paramName);
    }

    bool EffectRegistry::readParameterWords(
        const std::string& effectName, const std::string& paramName,
        uint64_t& cachedRevision, const EffectParam*& cachedParam,
        uint32_t* words, size_t wordCapacity, size_t& wordCount) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        wordCount = 0;
        if (words == nullptr || wordCapacity == 0)
            return false;

        if (cachedRevision != parameterStorageRevision)
        {
            const EffectConfig* effect = findEffectLocked(effectName);
            cachedParam = effect != nullptr ? findParamLocked(*effect, paramName) : nullptr;
            cachedRevision = parameterStorageRevision;
        }
        if (cachedParam == nullptr)
            return false;

        switch (cachedParam->getType())
        {
            case ParamType::Float:
            {
                const auto* value = static_cast<const FloatParam*>(cachedParam);
                std::memcpy(words, &value->value, sizeof(float));
                wordCount = 1;
                break;
            }
            case ParamType::FloatVec:
            {
                const auto* value = static_cast<const FloatVecParam*>(cachedParam);
                wordCount = std::min<size_t>(value->componentCount, std::min<size_t>(4, wordCapacity));
                std::memcpy(words, value->value, wordCount * sizeof(float));
                break;
            }
            case ParamType::Int:
            {
                const auto* value = static_cast<const IntParam*>(cachedParam);
                std::memcpy(words, &value->value, sizeof(int32_t));
                wordCount = 1;
                break;
            }
            case ParamType::IntVec:
            {
                const auto* value = static_cast<const IntVecParam*>(cachedParam);
                wordCount = std::min<size_t>(value->componentCount, std::min<size_t>(4, wordCapacity));
                std::memcpy(words, value->value, wordCount * sizeof(int32_t));
                break;
            }
            case ParamType::Uint:
            {
                const auto* value = static_cast<const UintParam*>(cachedParam);
                words[0] = value->value;
                wordCount = 1;
                break;
            }
            case ParamType::UintVec:
            {
                const auto* value = static_cast<const UintVecParam*>(cachedParam);
                wordCount = std::min<size_t>(value->componentCount, std::min<size_t>(4, wordCapacity));
                std::memcpy(words, value->value, wordCount * sizeof(uint32_t));
                break;
            }
            case ParamType::Bool:
            {
                const auto* value = static_cast<const BoolParam*>(cachedParam);
                words[0] = value->value ? 1u : 0u;
                wordCount = 1;
                break;
            }
        }
        return wordCount != 0;
    }

    std::vector<EffectParam*> EffectRegistry::getParametersForEffect(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EffectParam*> result;

        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return result;

        for (auto& param : effect->parameters)
            result.push_back(param.get());

        return result;
    }

    std::string EffectRegistry::authorText(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::string                 text;
        if (const EffectConfig* effect = findEffectLocked(effectName))
            for (const auto& param : effect->parameters)
                if (!param->text.empty())
                    text += (text.empty() ? "" : "\n\n") + param->text;
        return text;
    }

    bool EffectRegistry::hasEffect(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return findEffectLocked(name) != nullptr;
    }

    std::string EffectRegistry::getEffectFilePath(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        return effect ? effect->filePath : "";
    }

    std::string EffectRegistry::getEffectType(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        return effect ? effect->baseType : "";
    }

    bool EffectRegistry::isEffectBuiltIn(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        return effect ? (effect->type == EffectType::BuiltIn) : false;
    }

    bool EffectRegistry::hasEffectFailed(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        return effect ? effect->hasFailed() : false;
    }

    std::string EffectRegistry::getEffectError(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        return effect ? effect->compileError : "";
    }

    EffectOnDevice EffectRegistry::getEffectOnDevice(const std::string& name, const void* device) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(name);
        if (!effect)
            return {};
        const auto it = effect->onDevice.find(device);
        return it == effect->onDevice.end() ? EffectOnDevice{} : it->second;
    }

    void EffectRegistry::forgetDevice(const void* device)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
            effect.onDevice.erase(device);
    }

    void EffectRegistry::setEffectOnDevice(const std::string& name, const void* device, EffectOnDevice state)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffectLocked(name);
        if (effect)
            effect->onDevice[device] = std::move(state);
    }

    void EffectRegistry::setEffectError(const std::string& name, const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffectLocked(name);
        if (effect)
            effect->compileError = error;
    }

    void EffectRegistry::clearEffectErrors()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& effect : effects)
            effect.compileError.clear();
    }

    void EffectRegistry::ensureEffect(const std::string& instanceName, const std::string& effectType)
    {
        std::string type = effectType.empty() ? instanceName : effectType;

        std::string path;
        bool isBuiltIn = isBuiltInEffect(type);
        if (!isBuiltIn)
        {
            path = findEffectPath(type, pConfig);
            if (path.empty() || !std::filesystem::exists(path))
            {
                Logger::warn("EffectRegistry::ensureEffect: could not find effect file for: " + type);
                return;
            }
        }

        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* existing = findEffectLocked(instanceName);
        if (existing)
        {
            if (!isBuiltIn && !path.empty())
            {
                std::error_code ec;
                auto currentModTime = std::filesystem::last_write_time(path, ec);
                if (!ec && currentModTime != existing->fileModTime)
                {
                    Logger::info("EffectRegistry: shader file changed on disk, re-parsing: " + instanceName);
                    effects.erase(
                        std::remove_if(effects.begin(), effects.end(),
                            [&](const EffectConfig& e) { return e.instanceName == instanceName; }),
                        effects.end());
                }
                else
                {
                    return;
                }
            }
            else
            {
                return;
            }
        }

        if (isBuiltIn)
            initBuiltInEffect(instanceName, type);
        else
            initReshadeEffect(instanceName, path);
        ++parameterStorageRevision;
    }

    void EffectRegistry::removeEffect(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t oldSize = effects.size();
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [&](const EffectConfig& e) { return e.instanceName == name; }),
            effects.end());
        if (effects.size() != oldSize)
            ++parameterStorageRevision;
    }

    bool EffectRegistry::effectUsesMinPrecision(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(effectName);
        return effect && effect->usesMinPrecision;
    }

    bool EffectRegistry::getAllowHalfPrecision(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(effectName);
        return effect && effect->allowHalfPrecision;
    }

    bool EffectRegistry::listsAnotherGpu() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !plan.list.empty();
    }

    uint32_t EffectRegistry::getEffectGpu(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(effectName);
        return effect ? effect->gpu : 0;
    }

    void EffectRegistry::replanLocked()
    {
        plan.rows.clear();
        plan.ownGpus.clear();
        if (!selectedEffects.empty())
        {
            for (const std::string& name : selectedEffects)
            {
                const EffectConfig* effect = findEffectLocked(name);
                plan.rows.push_back(name);
                plan.ownGpus.push_back(effect ? effect->gpu : 0);
            }
        }
        else
        {
            for (const auto& effect : effects)
            {
                plan.rows.push_back(effect.instanceName);
                plan.ownGpus.push_back(effect.gpu);
            }
        }
        plan.anotherGpu = vkBasalt::assignsAnotherGpu(plan.rows, plan.ownGpus, plan.list);
    }

    void EffectRegistry::setEffectGpu(const std::string& effectName, uint32_t gpu)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (EffectConfig* effect = findEffectLocked(effectName))
            effect->gpu = gpu;
        replanLocked();
    }

    void EffectRegistry::setGpuList(const std::vector<GpuListEntry>& list)
    {
        std::lock_guard<std::mutex> lock(mutex);
        plan.list     = list;
        gpuListEdited = true;
        replanLocked();
    }

    std::optional<std::string> EffectRegistry::editedGpuList() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!gpuListEdited)
            return std::nullopt;
        std::string text;
        for (const GpuListEntry& entry : plan.list)
            text += (text.empty() ? "" : ",") + (entry.id ? formatGpuId(*entry.id) : entry.text);
        return text;
    }

    GpuPlan EffectRegistry::gpuPlan() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return plan;
    }

    void EffectRegistry::setAllowHalfPrecision(const std::string& effectName, bool allow)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffectLocked(effectName);
        if (effect)
            effect->allowHalfPrecision = allow;
    }

    std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffectLocked(effectName);
        if (effect)
            return effect->preprocessorDefs;
        static thread_local std::vector<PreprocessorDefinition> emptyDefs;
        emptyDefs.clear();
        return emptyDefs;
    }

    const std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(effectName);
        if (effect)
            return effect->preprocessorDefs;
        static thread_local const std::vector<PreprocessorDefinition> emptyDefs;
        return emptyDefs;
    }

    std::vector<PreprocessorDefinition> EffectRegistry::getCompilePreprocessorDefs(
        const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<PreprocessorDefinition> defs = setGlobalDefinitionsLocked();
        if (const EffectConfig* effect = findEffectLocked(effectName))
        {
            for (const auto& def : effect->preprocessorDefs)
            {
                if (!isSetGloballyLocked(def.name))
                    defs.push_back(def);
            }
        }
        return defs;
    }

    std::vector<PreprocessorDefinition> EffectRegistry::getGlobalDefinitions() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return globalDefs;
    }

    std::vector<PreprocessorDefinition> EffectRegistry::getSavedGlobalDefinitions() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<PreprocessorDefinition> saved;
        for (const auto& global : globalDefs)
        {
            if (globalDefinitionProblem(global.name, global.value).empty()
                && (global.value != global.defaultValue || explicitGlobals.count(global.name) != 0))
                saved.push_back(global);
        }
        return saved;
    }

    bool EffectRegistry::isSetGlobally(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return isSetGloballyLocked(name);
    }

    std::string EffectRegistry::addGlobalDefinition(const std::string& name, const std::string& value)
    {
        if (const std::string problem = globalDefinitionProblem(name, value); !problem.empty())
            return problem;

        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& global : globalDefs)
        {
            if (global.name == name)
                return name + " is already a global definition";
        }
        globalDefs.push_back({name, value, "", ""});
        return "";
    }

    void EffectRegistry::removeGlobalDefinition(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        globalDefs.erase(std::remove_if(globalDefs.begin(), globalDefs.end(),
                                        [&](const PreprocessorDefinition& global) { return global.name == name && global.defaultValue.empty(); }),
                         globalDefs.end());
    }

    std::vector<std::string> EffectRegistry::globalDefinitionUsers(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string>    users;
        const auto                  found = globalUsers.find(name);
        if (found == globalUsers.end())
            return users;
        for (const std::string& effect : selectedEffects)
            if (found->second.count(effect) != 0 && std::find(users.begin(), users.end(), effect) == users.end())
                users.push_back(effect);
        return users;
    }

    void EffectRegistry::addGlobalDefaultLocked(const PreprocessorDefinition& def, const std::string& effectName)
    {
        globalUsers[def.name].insert(effectName);
        for (auto& global : globalDefs)
        {
            if (global.name != def.name)
                continue;
            if (global.defaultValue.empty())
                global.defaultValue = def.defaultValue;
            return;
        }
        globalDefs.push_back({def.name, def.defaultValue, def.defaultValue, ""});
    }

    std::optional<std::vector<PreprocessorDefinition>> EffectRegistry::staleParameterReadDefinitions(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffectLocked(effectName);
        if (!effect || effect->type != EffectType::ReShade || !effect->compileError.empty())
            return std::nullopt;
        std::vector<PreprocessorDefinition> readDefs = parameterReadDefinitionsLocked(*effect);
        if (parameterReadDefines(readDefs) == effect->parameterDefines)
            return std::nullopt;
        return readDefs;
    }

    void EffectRegistry::refreshParameters(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect || effect->type != EffectType::ReShade || !effect->compileError.empty())
            return;
        const std::vector<PreprocessorDefinition> readDefs = parameterReadDefinitionsLocked(*effect);
        std::vector<std::pair<std::string, std::string>> defines = parameterReadDefines(readDefs);
        if (defines == effect->parameterDefines)
            return;

        Config carried(*pConfig);
        carried.setFallback(pConfig);
        std::set<std::string> atDefault;
        for (const auto& param : effect->parameters)
        {
            if (!param->hasChanged())
            {
                atDefault.insert(param->name);
                continue;
            }
            for (const auto& [suffix, value] : param->serialize())
                carried.setOverride(effectName + "." + (suffix.empty() ? param->name : suffix), value);
        }
        effect->parameters = parseReshadeEffect(effectName, effect->filePath, &carried, readDefs);
        for (const auto& param : effect->parameters)
        {
            if (atDefault.count(param->name) != 0)
                param->resetToDefault();
        }
        effect->parameterDefines = std::move(defines);
        ++parameterStorageRevision;
        Logger::info("read the parameters of " + effectName + " again under its changed definitions: " +
                     std::to_string(effect->parameters.size()) + " parameters");
    }

    std::vector<PreprocessorDefinition> EffectRegistry::parameterReadDefinitionsLocked(const EffectConfig& effect) const
    {
        std::vector<PreprocessorDefinition> defs = setGlobalDefinitionsLocked();
        for (const auto& def : effect.preprocessorDefs)
        {
            if (def.value != def.defaultValue && !isSetGloballyLocked(def.name))
                defs.push_back(def);
        }
        return defs;
    }

    std::vector<PreprocessorDefinition> EffectRegistry::setGlobalDefinitionsLocked() const
    {
        std::vector<PreprocessorDefinition> set;
        for (const auto& global : globalDefs)
        {
            if (global.value != global.defaultValue && globalDefinitionProblem(global.name, global.value).empty())
                set.push_back(global);
        }
        return set;
    }

    bool EffectRegistry::isSetGloballyLocked(const std::string& name) const
    {
        return std::any_of(globalDefs.begin(), globalDefs.end(), [&](const PreprocessorDefinition& global) {
            return global.name == name && global.value != global.defaultValue && globalDefinitionProblem(global.name, global.value).empty();
        });
    }

    void EffectRegistry::setPreprocessorDefValue(const std::string& effectName, const std::string& macroName, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (effectName.empty())
        {
            for (auto& global : globalDefs)
            {
                if (global.name == macroName)
                {
                    global.value = value;
                    Logger::debug("global definition " + macroName + " set to " + value);
                }
            }
            return;
        }
        EffectConfig* effect = findEffectLocked(effectName);
        if (!effect)
            return;

        for (auto& def : effect->preprocessorDefs)
        {
            if (def.name == macroName)
            {
                def.value = value;
                Logger::debug("preprocessor definition " + macroName + " of " + effectName + " set to " + value);
                return;
            }
        }
    }

    void EffectRegistry::setSelectedEffects(const std::vector<std::string>& effects)
    {
        std::lock_guard<std::mutex> lock(mutex);
        selectedEffects = effects;
        replanLocked();
    }

    void EffectRegistry::initializeSelectedEffectsFromConfig()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (initializedFromConfig)
                return;
        }
        selectEffectsFromConfig();
    }

    void EffectRegistry::selectEffectsFromConfig()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!pConfig)
                return;
        }

        std::vector<std::string> configEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});

        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        {
            std::lock_guard<std::mutex> lock(mutex);
            selectedEffects = configEffects;
            plan.list       = parseGpuList(pConfig->getOption<std::string>("gpus", ""));
            gpuListEdited   = false;
            for (EffectConfig& effect : effects)
                effect.gpu = rowGpuFromConfig(effect.instanceName, pConfig);
        }

        for (const auto& effectName : configEffects)
            ensureEffect(effectName);

        {
            std::lock_guard<std::mutex> lock(mutex);
            replanLocked();
            for (const std::string& problem : gpuAssignmentProblems(plan.rows, plan.ownGpus, plan.list))
                if (initializedFromConfig)
                    Logger::warn(problem + ", so every effect runs on the game's GPU");
        }

        for (const auto& effectName : configEffects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            setEffectEnabled(effectName, enabled);
        }

        initializedFromConfig = true;
        Logger::debug("EffectRegistry: selected " + std::to_string(configEffects.size()) +
                      " effects from config (" + std::to_string(disabledEffects.size()) + " disabled)");
    }

}
