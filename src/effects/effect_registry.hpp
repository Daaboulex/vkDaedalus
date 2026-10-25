#ifndef EFFECT_REGISTRY_HPP_INCLUDED
#define EFFECT_REGISTRY_HPP_INCLUDED

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <mutex>
#include <filesystem>

#include "effect_config.hpp"
#include "gpu_assignment.hpp"
#include "config.hpp"

namespace vkBasalt
{
    struct GpuPlan
    {
        std::vector<GpuListEntry> list;
        std::vector<std::string>  rows;
        std::vector<uint32_t>     ownGpus;
        bool                      anotherGpu = false;
    };

    class EffectRegistry
    {
    public:
        void initialize(Config* pConfig);
        static std::vector<std::string> reshadeEffectFilesOf(Config* pConfig);

        const std::vector<EffectConfig>& getAllEffectsUnlocked() const
        {
            return effects;
        }

        std::vector<const EffectConfig*> getEnabledEffects() const;

        std::vector<std::unique_ptr<EffectParam>> getAllParameters() const;

        void setEffectEnabled(const std::string& effectName, bool enabled);

        bool isEffectEnabled(const std::string& effectName) const;

        std::map<std::string, bool> getEffectEnabledStates() const;
        std::vector<std::string> getActiveEffects() const;

        void setParameterValue(const std::string& effectName, const std::string& paramName, float value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, int value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, bool value);

        EffectParam* getParameter(const std::string& effectName, const std::string& paramName);
        const EffectParam* getParameter(const std::string& effectName, const std::string& paramName) const;

        bool readParameterWords(
            const std::string& effectName, const std::string& paramName,
            uint64_t& cachedRevision, const EffectParam*& cachedParam,
            uint32_t* words, size_t wordCapacity, size_t& wordCount) const;

        std::vector<EffectParam*> getParametersForEffect(const std::string& effectName);
        std::string               authorText(const std::string& effectName) const;

        Config* getConfig() const { return pConfig; }

        static bool isBuiltInEffect(const std::string& name);
        std::string effectFilePathFor(const std::string& effectType) const;

        void ensureEffect(const std::string& name, const std::string& effectPath = "");

        void removeEffect(const std::string& name);

        bool hasEffect(const std::string& name) const;

        std::string getEffectFilePath(const std::string& name) const;

        std::string getEffectType(const std::string& name) const;

        bool isEffectBuiltIn(const std::string& name) const;

        bool hasEffectFailed(const std::string& name) const;

        std::string getEffectError(const std::string& name) const;

        void setEffectError(const std::string& name, const std::string& error);
        void clearEffectErrors();

        EffectOnDevice getEffectOnDevice(const std::string& name, const void* device) const;

        void setEffectOnDevice(const std::string& name, const void* device, EffectOnDevice state);
        void forgetDevice(const void* device);

        bool effectUsesMinPrecision(const std::string& effectName) const;
        bool getAllowHalfPrecision(const std::string& effectName) const;
        void setAllowHalfPrecision(const std::string& effectName, bool allow);

        bool    listsAnotherGpu() const;
        GpuPlan gpuPlan() const;
        uint32_t getEffectGpu(const std::string& effectName) const;
        void     setEffectGpu(const std::string& effectName, uint32_t gpu);
        void     setGpuList(const std::vector<GpuListEntry>& list);
        std::optional<std::string> editedGpuList() const;

        std::vector<PreprocessorDefinition>& getPreprocessorDefs(const std::string& effectName);
        const std::vector<PreprocessorDefinition>& getPreprocessorDefs(const std::string& effectName) const;
        std::vector<PreprocessorDefinition> getCompilePreprocessorDefs(const std::string& effectName) const;

        void setPreprocessorDefValue(const std::string& effectName, const std::string& macroName, const std::string& value);

        std::vector<PreprocessorDefinition> getGlobalDefinitions() const;
        std::vector<PreprocessorDefinition> getSavedGlobalDefinitions() const;
        bool isSetGlobally(const std::string& name) const;
        std::vector<std::string> globalDefinitionUsers(const std::string& name) const;
        std::string addGlobalDefinition(const std::string& name, const std::string& value);
        void removeGlobalDefinition(const std::string& name);

        std::optional<std::vector<PreprocessorDefinition>> staleParameterReadDefinitions(const std::string& effectName) const;
        void refreshParameters(const std::string& effectName);

        const std::vector<std::string>& getSelectedEffects() const { return selectedEffects; }
        void setSelectedEffects(const std::vector<std::string>& effects);


        void initializeSelectedEffectsFromConfig();
        void selectEffectsFromConfig();

    private:
        std::vector<EffectConfig> effects;
        std::vector<PreprocessorDefinition> globalDefs;
        std::set<std::string> explicitGlobals;
        std::map<std::string, std::set<std::string>> globalUsers;
        std::vector<std::string> selectedEffects;
        bool initializedFromConfig = false;
        GpuPlan plan;
        bool    gpuListEdited = false;
        void    replanLocked();
        Config* pConfig = nullptr;
        uint64_t parameterStorageRevision = 1;
        mutable std::mutex mutex;

        void initBuiltInEffect(const std::string& instanceName, const std::string& effectType);

        void initReshadeEffect(const std::string& name, const std::string& path);

        void addGlobalDefaultLocked(const PreprocessorDefinition& def, const std::string& effectName);
        std::vector<PreprocessorDefinition> setGlobalDefinitionsLocked() const;
        bool isSetGloballyLocked(const std::string& name) const;
        std::vector<PreprocessorDefinition> parameterReadDefinitionsLocked(const EffectConfig& effect) const;

        EffectConfig* findEffectLocked(const std::string& effectName);
        const EffectConfig* findEffectLocked(const std::string& effectName) const;
        EffectParam* findParamLocked(EffectConfig& effect, const std::string& paramName);
        const EffectParam* findParamLocked(const EffectConfig& effect, const std::string& paramName) const;
    };

}

#endif
