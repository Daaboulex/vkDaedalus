#ifndef RESHADE_PARSER_HPP_INCLUDED
#define RESHADE_PARSER_HPP_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <utility>

#include "effects/effect_config.hpp"
#include "effects/params/effect_param.hpp"
#include "config.hpp"
#include "shader_test_result.hpp"

namespace vkBasalt
{
    std::vector<std::pair<std::string, std::string>> parameterReadDefines(const std::vector<PreprocessorDefinition>& defs);

    std::vector<std::unique_ptr<EffectParam>> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig,
        const std::vector<PreprocessorDefinition>& defs);

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath);
    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths);

    std::vector<PreprocessorDefinition> extractPreprocessorDefinitions(
        const std::string& effectName,
        const std::string& effectPath);

}

#endif
