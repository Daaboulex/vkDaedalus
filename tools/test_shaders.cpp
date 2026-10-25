#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "reshade/effect_module.hpp"
#include "reshade_compile_defines.hpp"
#include "shader_cache.hpp"
#include "logger.hpp"
#include "reshade_effect_files.hpp"

namespace fs = std::filesystem;


static std::string g_dumpSpirvDir;
static bool g_cacheBench = false;
static bool g_cacheVerify = false;
static bool g_relaxPrecision = false;
static bool g_requireLiveUniforms = false;

static const std::vector<std::pair<std::string, std::string>>& standardMacroPairs()
{
    static const std::vector<std::pair<std::string, std::string>> pairs =
        vkBasalt::reshadeCompileDefines({1920, 1080}, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, {});
    return pairs;
}



struct ShaderManagerConfig
{
    std::vector<std::string> parentDirectories;
    std::vector<std::string> discoveredShaderPaths;
    std::vector<std::string> discoveredTexturePaths;
};

static ShaderManagerConfig loadShaderManagerConfig()
{
    ShaderManagerConfig config;
    const char* home = getenv("HOME");
    if (!home)
        return config;

    std::string configPath = std::string(home) + "/.config/vkdaedalus/shader_manager.conf";
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        std::cerr << "warning: could not open " << configPath << "\n";
        return config;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key);
        trim(value);

        if (key == "parentDir" && !value.empty())
            config.parentDirectories.push_back(value);
        else if (key == "shaderPath" && !value.empty())
            config.discoveredShaderPaths.push_back(value);
        else if (key == "texturePath" && !value.empty())
            config.discoveredTexturePaths.push_back(value);
    }

    return config;
}


enum class ErrorCategory
{
    Preprocessor,
    Parse,
    Exception,
    Unsupported
};

static const char* categoryName(ErrorCategory cat)
{
    switch (cat)
    {
        case ErrorCategory::Preprocessor: return "PREPROCESSOR";
        case ErrorCategory::Parse:        return "PARSE";
        case ErrorCategory::Exception:    return "EXCEPTION";
        case ErrorCategory::Unsupported:  return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

static ErrorCategory categoryOf(const std::string& error)
{
    if (error.rfind("Failed to load shader file", 0) == 0 || error.rfind("Preprocessor errors", 0) == 0)
        return ErrorCategory::Preprocessor;
    if (error.rfind("Parse errors", 0) == 0)
        return ErrorCategory::Parse;
    return ErrorCategory::Unsupported;
}

struct TestResult
{
    std::string name;
    std::string path;
    bool success = false;
    bool usesDepth = false;
    std::string errorMessage;
    ErrorCategory category = ErrorCategory::Parse;
    double milliseconds = 0.0;
};


static TestResult testShader(
    const std::string& effectName,
    const std::string& effectPath,
    const std::vector<std::string>& includePaths)
{
    TestResult result;
    result.name = effectName;
    result.path = effectPath;

    const auto compileStart = std::chrono::steady_clock::now();
    struct CompileTimer
    {
        std::chrono::steady_clock::time_point start;
        double* out;
        ~CompileTimer() { *out = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
    } compileTimer{compileStart, &result.milliseconds};

    try
    {
        const auto compiled = vkBasalt::compileReshadeEffectUncached(effectPath, standardMacroPairs(), includePaths,
                                                                     g_relaxPrecision, g_requireLiveUniforms);
        if (!compiled->ok())
        {
            result.success      = false;
            result.errorMessage = compiled->error;
            result.category     = categoryOf(compiled->error);
            return result;
        }

        const reshadefx::effect_module& module = compiled->module;
        if (g_requireLiveUniforms
            && (module.uniforms.empty() || !module.spec_constants.empty()
                || module.total_uniform_size == 0))
        {
            result.success = false;
            result.errorMessage =
                "UI uniforms were not lowered into the live uniform buffer";
            result.category = ErrorCategory::Unsupported;
            return result;
        }
        result.errorMessage = compiled->warnings;

        if (!g_dumpSpirvDir.empty())
        {
            for (const auto& [entryPointName, words] : compiled->entryPointSpirv)
            {
                std::filesystem::path out =
                    std::filesystem::path(g_dumpSpirvDir) / (result.name + "__" + entryPointName + ".spv");
                std::ofstream spv(out, std::ios::binary);
                spv.write(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
            }
        }

        result.usesDepth = compiled->usesDepth;
        result.success = true;
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.errorMessage = "Exception: " + std::string(e.what());
        result.category = ErrorCategory::Exception;
    }
    catch (...)
    {
        result.success = false;
        result.errorMessage = "Unknown exception during compilation";
        result.category = ErrorCategory::Exception;
    }

    return result;
}


static std::vector<fs::path> collectFxFiles(const std::string& dir)
{
    std::vector<fs::path> files;
    if (!fs::is_directory(dir))
    {
        std::cerr << "warning: not a directory: " << dir << "\n";
        return files;
    }

    std::vector<fs::path> dirs = {dir};
    std::error_code walkEc;
    fs::recursive_directory_iterator it(dir, fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied, walkEc);
    for (const fs::recursive_directory_iterator end; !walkEc && it != end; it.increment(walkEc))
    {
        std::error_code entryEc;
        if (it->is_directory(entryEc))
            dirs.push_back(it->path());
    }
    if (walkEc)
        std::cerr << "warning: could not walk " << dir << ": " << walkEc.message() << "\n";

    for (const auto& d : dirs)
    {
        std::error_code listEc;
        const auto found = vkBasalt::reshadeEffectFilesIn(d, listEc);
        if (listEc)
            std::cerr << "warning: could not read " << d << ": " << listEc.message() << "\n";
        files.insert(files.end(), found.begin(), found.end());
    }

    std::sort(files.begin(), files.end());
    return files;
}


int main(int argc, char* argv[])
{
    std::vector<std::string> shaderDirs;
    std::vector<std::string> includePaths;

    if (argc > 1)
    {
        for (int i = 1; i < argc; i++)
        {
            if (std::string(argv[i]) == "--dump-spirv" && (i + 1) < argc)
            {
                g_dumpSpirvDir = argv[++i];
                continue;
            }
            if (std::string(argv[i]) == "--cache-bench")
            {
                g_cacheBench = true;
                continue;
            }
            if (std::string(argv[i]) == "--cache-verify")
            {
                g_cacheVerify = true;
                continue;
            }
            if (std::string(argv[i]) == "--relax-precision")
            {
                g_relaxPrecision = true;
                continue;
            }
            if (std::string(argv[i]) == "--require-live-uniforms")
            {
                g_requireLiveUniforms = true;
                continue;
            }
            if (std::string(argv[i]) == "--include" && (i + 1) < argc)
            {
                includePaths.push_back(argv[++i]);
                continue;
            }
            shaderDirs.push_back(argv[i]);
            includePaths.push_back(argv[i]);
        }
    }
    else
    {
        auto config = loadShaderManagerConfig();
        includePaths = config.discoveredShaderPaths;

        if (includePaths.empty())
        {
            std::string defaultDir = "/etc/vkdaedalus/reshade/Shaders";
            includePaths.push_back(defaultDir);
        }

        shaderDirs = includePaths;

        for (const auto& tp : config.discoveredTexturePaths)
            includePaths.push_back(tp);
    }

    std::cout << "Include paths:\n";
    for (const auto& p : includePaths)
        std::cout << "  " << p << "\n";
    std::cout << "\n";

    std::vector<fs::path> allFiles;
    for (const auto& dir : shaderDirs)
    {
        auto files = collectFxFiles(dir);
        allFiles.insert(allFiles.end(), files.begin(), files.end());
    }

    for (auto& f : allFiles)
    {
        std::error_code ec;
        const fs::path canonical = fs::canonical(f, ec);
        if (!ec)
            f = canonical;
    }
    std::sort(allFiles.begin(), allFiles.end());
    allFiles.erase(std::unique(allFiles.begin(), allFiles.end()), allFiles.end());

    if (allFiles.empty())
    {
        std::cerr << "No .fx files found in the specified directories.\n";
        return 1;
    }

    std::cout << "Testing " << allFiles.size() << " shader(s)...\n\n";

    int passCount = 0;
    int failCount = 0;
    int warnCount = 0;
    std::vector<double> compileTimes;
    std::vector<std::string> compiledPaths;
    int depthCount = 0;
    std::vector<TestResult> failures;
    std::vector<TestResult> warnings;

    for (const auto& fxPath : allFiles)
    {
        std::string effectName = fxPath.stem().string();
        std::string effectPath = fxPath.string();

        TestResult result = testShader(effectName, effectPath, includePaths);

        if (result.success)
        {
            compileTimes.push_back(result.milliseconds);
            compiledPaths.push_back(effectPath);
        }

        if (result.success)
        {
            if (!result.errorMessage.empty())
            {
                std::cout << "WARN  " << effectName << "\n";
                warnCount++;
                warnings.push_back(result);
            }
            else
            {
                std::cout << "PASS  " << effectName;
                if (result.usesDepth)
                {
                    std::cout << "  [uses depth]";
                    depthCount++;
                }
                std::cout << "\n";
            }
            passCount++;
        }
        else
        {
            std::cout << "FAIL  " << effectName << "  [" << categoryName(result.category) << "]\n";
            failCount++;
            failures.push_back(result);
        }
    }


    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  RESULTS: " << passCount << " passed, " << failCount << " failed";
    if (warnCount > 0)
        std::cout << ", " << warnCount << " warnings";
    if (depthCount > 0)
        std::cout << " (" << depthCount << " use depth)";
    std::cout << "\n";
    std::cout << "  Total:   " << allFiles.size() << " shaders\n";

    int cacheMismatches = 0;
    if (g_cacheVerify)
    {
        for (const std::string& path : compiledPaths)
        {
            std::shared_ptr<const vkBasalt::CompiledReshadeEffect> entry;
            try
            {
                entry = vkBasalt::getOrCompileReshadeEffect(
                    path, standardMacroPairs(), includePaths,
                    g_relaxPrecision, g_requireLiveUniforms);
            }
            catch (const std::exception& e)
            {
                std::cout << "CACHE-MISMATCH  " << path << "  the cached compile threw: " << e.what() << "\n";
                cacheMismatches++;
                continue;
            }
            if (!entry || !entry->ok())
            {
                std::cout << "CACHE-MISMATCH  " << path << "  the cached compile failed where the plain one passed: "
                          << (entry ? entry->error : std::string("no result")) << "\n";
                cacheMismatches++;
                continue;
            }

            std::string field = vkBasalt::cacheRoundTripMismatch(*entry);
            if (!field.empty())
            {
                std::cout << "CACHE-MISMATCH  " << path << "  " << field << "\n";
                cacheMismatches++;
            }
        }
        std::cout << "  CACHE-VERIFY: " << compiledPaths.size() << " compiled, " << cacheMismatches
                  << " round-trip mismatch(es)\n";
    }

    if (g_cacheBench)
    {
        std::vector<double> cold, warm;
        const std::vector<std::pair<std::string, std::string>>& macros = standardMacroPairs();

        for (const std::string& path : compiledPaths)
        {
            std::shared_ptr<const vkBasalt::CompiledReshadeEffect> first, second;
            std::chrono::steady_clock::time_point t0, t1, t2;

            try
            {
                t0 = std::chrono::steady_clock::now();
                first = vkBasalt::getOrCompileReshadeEffect(
                    path, macros, includePaths,
                    g_relaxPrecision, g_requireLiveUniforms);
                t1 = std::chrono::steady_clock::now();
                second = vkBasalt::getOrCompileReshadeEffect(
                    path, macros, includePaths,
                    g_relaxPrecision, g_requireLiveUniforms);
                t2 = std::chrono::steady_clock::now();
            }
            catch (const std::exception&)
            {
                continue;
            }

            if (!first || !first->ok())
                continue;

            cold.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            warm.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
        }

        if (!cold.empty())
        {
            std::sort(cold.begin(), cold.end());
            std::sort(warm.begin(), warm.end());
            const auto at = [](std::vector<double>& v, double p) { return v[static_cast<size_t>(p / 100.0 * (v.size() - 1))]; };
            double coldTotal = 0.0, warmTotal = 0.0;
            for (double d : cold) coldTotal += d;
            for (double d : warm) warmTotal += d;
            std::cout << "  CACHE n=" << cold.size()
                      << " cold p50=" << at(cold, 50) << " p95=" << at(cold, 95) << " max=" << cold.back() << " total=" << coldTotal
                      << " | warm p50=" << at(warm, 50) << " p95=" << at(warm, 95) << " max=" << warm.back() << " total=" << warmTotal << "\n";
        }
    }

    std::vector<double> times = compileTimes;
    double totalMs = 0.0;
    for (double t : times)
        totalMs += t;

    if (!times.empty())
    {
        std::sort(times.begin(), times.end());
        const auto pct = [&times](double p) {
            size_t i = static_cast<size_t>(p / 100.0 * (times.size() - 1));
            return times[i];
        };
        std::cout << "  COMPILE ms: p50=" << pct(50) << " p95=" << pct(95) << " p99=" << pct(99)
                  << " max=" << times.back() << " total=" << totalMs << "\n";
    }
    std::cout << "========================================\n";


    if (!failures.empty())
    {
        std::vector<std::pair<ErrorCategory, std::vector<const TestResult*>>> grouped;
        auto addToGroup = [&](ErrorCategory cat, const TestResult* r) {
            for (auto& [gc, vec] : grouped)
            {
                if (gc == cat) { vec.push_back(r); return; }
            }
            grouped.push_back({cat, {r}});
        };

        for (const auto& f : failures)
            addToGroup(f.category, &f);

        std::cout << "\n--- FAILURES BY CATEGORY ---\n";
        for (const auto& [cat, results] : grouped)
        {
            std::cout << "\n[" << categoryName(cat) << "] (" << results.size() << " shader(s))\n";
            for (const auto* r : results)
            {
                std::cout << "  " << r->name << "\n";
                std::cout << "    Path:  " << r->path << "\n";
                std::string msg = r->errorMessage;
                size_t pos = 0;
                bool first = true;
                while (pos < msg.size())
                {
                    size_t nl = msg.find('\n', pos);
                    std::string line = (nl == std::string::npos)
                        ? msg.substr(pos) : msg.substr(pos, nl - pos);
                    if (first)
                        std::cout << "    Error: " << line << "\n";
                    else
                        std::cout << "           " << line << "\n";
                    first = false;
                    if (nl == std::string::npos)
                        break;
                    pos = nl + 1;
                }
            }
        }
    }


    if (!warnings.empty())
    {
        std::cout << "\n--- WARNINGS ---\n";
        for (const auto& w : warnings)
        {
            std::cout << "  " << w.name << "\n";
            std::string msg = w.errorMessage;
            size_t pos = 0;
            bool first = true;
            while (pos < msg.size())
            {
                size_t nl = msg.find('\n', pos);
                std::string line = (nl == std::string::npos)
                    ? msg.substr(pos) : msg.substr(pos, nl - pos);
                if (first)
                    std::cout << "    " << line << "\n";
                else
                    std::cout << "    " << line << "\n";
                first = false;
                if (nl == std::string::npos)
                    break;
                pos = nl + 1;
            }
        }
    }

    return (failCount > 0 || cacheMismatches > 0) ? 1 : 0;
}
