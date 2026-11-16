#include "mako_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

#include "toml_edit.hpp"

namespace vkBasalt
{
    namespace
    {
        namespace fs = std::filesystem;

        const std::string emptyMakoText      = "version = 2\n\n[global]\n";
        const std::string makoFile           = "MAKO's file";
        const std::string makoDllUnderCommon = "Lossless Scaling/Lossless.dll";
        const WriteRules  makoWriteRules{true, true, fs::perms::owner_read | fs::perms::owner_write};
        const std::string headroomRefusal     = "gpu_headroom_percent must be 0, 5, 10, 15 or 20";
        const std::string powerTablesRefusal  = "power profiles require handheld and docked tables";
        const std::string sharedNamesRefusal  = "power profile identity must remain shared";
        const std::string unparsedRefusal     = "unable to parse configuration\n- ";
        const std::string wrongVersionRefusal = "unsupported configuration version";

        constexpr std::array<std::string_view, 11> launchersMakoLeavesAlone = {"ubisoftconnect.exe",
                                                                               "upc.exe",
                                                                               "uplaywebcore.exe",
                                                                               "redprelauncher.exe",
                                                                               "redlauncher.exe",
                                                                               "socialclubhelper.exe",
                                                                               "rockstarservice.exe",
                                                                               "rockstarerrorhandler.exe",
                                                                               "eadesktop.exe",
                                                                               "ealauncher.exe",
                                                                               "eabackgroundservice.exe"};

        constexpr std::array<std::string_view, 8> supplyTypes = {"Mains", "USB", "USB_C", "USB_PD", "USB_DCP", "USB_CDP", "USB_ACA", "Wireless"};

        struct ChoiceKey
        {
            std::string_view                       key;
            std::span<const FrameGenerationChoice> choices;
            std::string_view                       refusal;
            bool                                   refusalNamesTheValue;
        };

        constexpr std::array<ChoiceKey, 7> choiceKeys = {{
            {"frame_generation_model", makoFrameGenerationModels, "frame_generation_model must be 'lossless-scaling' or 'mako-fg'", false},
            {"mako_fg_mode", makoFgModes, "mako_fg_mode must be 'quality', 'balanced', 'performance' or 'extra-performance'", false},
            {"scaling_method", makoScalingMethods, "unknown scaling method: ", true},
            {"hdr_quality", makoHdrQualities, "unknown HDR quality: ", true},
            {"adaptive_fractional_real_frame_priority", makoRealFramePriorities, "unknown Adaptive Fractional real-frame priority: ", true},
            {"gamescope_vrr_mode", makoGamescopeVrrModes, "unknown Gamescope VRR mode: ", true},
            {"pacing", makoPacings, "unknown pacing method: ", true},
        }};

        template<typename Visit>
        void forEachSetting(Visit&& visit)
        {
            visit("gpu", &MakoSettings::gpu);
            visit("multiplier", &MakoSettings::multiplier);
            visit("frame_generation_provisioned", &MakoSettings::frameGenerationProvisioned);
            visit("frame_generation_enabled", &MakoSettings::frameGenerationEnabled);
            visit("frame_generation_model", &MakoSettings::frameGenerationModel);
            visit("mako_fg_mode", &MakoSettings::makoFgMode);
            visit("scaling_enabled", &MakoSettings::scalingEnabled);
            visit("swapchain_image_count_compatibility", &MakoSettings::swapchainImageCountCompatibility);
            visit("mailbox_presentation_compatibility", &MakoSettings::mailboxPresentationCompatibility);
            visit("scaling_method", &MakoSettings::scalingMethod);
            visit("scaling_factor", &MakoSettings::scalingFactor);
            visit("scaling_supersampling", &MakoSettings::scalingSupersampling);
            visit("scaling_sharpness", &MakoSettings::scalingSharpness);
            visit("hdr_quality", &MakoSettings::hdrQuality);
            visit("frame_generation_refresh_threshold", &MakoSettings::frameGenerationRefreshThreshold);
            visit("base_fps_cap", &MakoSettings::baseFpsCap);
            visit("gpu_headroom_percent", &MakoSettings::gpuHeadroomPercent);
            visit("adaptive", &MakoSettings::adaptive);
            visit("adaptive_auto_base_fps_cap", &MakoSettings::adaptiveAutoBaseFpsCap);
            visit("adaptive_fractional_real_frame_priority", &MakoSettings::adaptiveFractionalRealFramePriority);
            visit("target_fps", &MakoSettings::targetFps);
            visit("adaptive_target_refresh_rate", &MakoSettings::adaptiveTargetRefreshRate);
            visit("adaptive_max_multiplier", &MakoSettings::adaptiveMaxMultiplier);
            visit("adaptive_stable_cadence", &MakoSettings::adaptiveStableCadence);
            visit("gamescope_vrr_mode", &MakoSettings::gamescopeVrrMode);
            visit("dynamic_cadence_recovery", &MakoSettings::dynamicCadenceRecovery);
            visit("dynamic_cadence_probe_interval_seconds", &MakoSettings::dynamicCadenceProbeIntervalSeconds);
            visit("ultra_performance", &MakoSettings::ultraPerformance);
            visit("flow_scale", &MakoSettings::flowScale);
            visit("performance_mode", &MakoSettings::performanceMode);
            visit("pacing", &MakoSettings::pacing);
        }

        template<typename Value>
        bool sameValue(const Value& a, const Value& b)
        {
            if constexpr (std::is_floating_point_v<Value>)
                return a == b || (std::isnan(a) && std::isnan(b));
            else
                return a == b;
        }

        template<typename Value>
        std::optional<std::string> valueText(const Value& value, bool inPowerSet, bool& clean)
        {
            if constexpr (std::is_same_v<Value, bool>)
                return std::string(value ? "true" : "false");
            else if constexpr (std::is_floating_point_v<Value>)
                return floatText(value);
            else if constexpr (std::is_integral_v<Value>)
                return std::to_string(value);
            else if constexpr (std::is_same_v<Value, std::string>)
                return optionalString(value, clean);
            else if (!value && inPowerSet)
                return std::string("\"\"");
            else
                return optionalString(value, clean);
        }

        char asciiLower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        }

        bool asciiEqual(std::string_view a, std::string_view b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) { return asciiLower(x) == asciiLower(y); });
        }

        bool namesAnExe(std::string_view name)
        {
            return name.size() >= 4 && asciiEqual(name.substr(name.size() - 4), ".exe");
        }

        std::string makoEntryFor(const std::string& game)
        {
            return "/" + game;
        }

        std::optional<std::string> choiceRefusal(std::string_view key, const std::string& value)
        {
            for (const ChoiceKey& choice : choiceKeys)
                if (choice.key == key && std::none_of(choice.choices.begin(), choice.choices.end(), [&](const FrameGenerationChoice& known) {
                        return known.name == value;
                    }))
                    return std::string(choice.refusal) + (choice.refusalNamesTheValue ? value : "");
            return std::nullopt;
        }

        bool validated(const MakoSettings& settings, std::string& problem)
        {
            using Limits       = MakoLimits;
            const auto between = [](std::string_view key, const std::string& lowest, const std::string& highest) {
                return std::string(key) + " must be between " + lowest + " and " + highest;
            };
            const auto zeroOrUpTo = [](std::string_view key, uint32_t highest) {
                return std::string(key) + " must be 0 or between 1 and " + std::to_string(highest);
            };
            if (settings.multiplier < Limits::lowestMultiplier || settings.multiplier > Limits::highestMultiplier)
                return refused(problem, between("multiplier", std::to_string(Limits::lowestMultiplier), std::to_string(Limits::highestMultiplier)));
            if (!(settings.scalingFactor >= Limits::lowestScalingFactor && settings.scalingFactor <= Limits::highestScalingFactor))
                return refused(problem, between("scaling_factor", floatText(Limits::lowestScalingFactor), floatText(Limits::highestScalingFactor)));
            if (!(settings.scalingSharpness >= Limits::lowestScalingSharpness && settings.scalingSharpness <= Limits::highestScalingSharpness))
                return refused(problem,
                               between("scaling_sharpness", floatText(Limits::lowestScalingSharpness), floatText(Limits::highestScalingSharpness)));
            if (settings.baseFpsCap > Limits::highestBaseFpsCap)
                return refused(problem, zeroOrUpTo("base_fps_cap", Limits::highestBaseFpsCap));
            if (settings.frameGenerationRefreshThreshold > Limits::highestRefreshThreshold)
                return refused(problem, zeroOrUpTo("frame_generation_refresh_threshold", Limits::highestRefreshThreshold));
            if (settings.targetFps < Limits::lowestTargetFps || settings.targetFps > Limits::highestTargetFps)
                return refused(problem, between("target_fps", std::to_string(Limits::lowestTargetFps), std::to_string(Limits::highestTargetFps)));
            if (settings.adaptiveMaxMultiplier < Limits::lowestAdaptiveMaxMultiplier
                || settings.adaptiveMaxMultiplier > Limits::highestAdaptiveMaxMultiplier)
                return refused(problem,
                               between("adaptive_max_multiplier",
                                       std::to_string(Limits::lowestAdaptiveMaxMultiplier),
                                       std::to_string(Limits::highestAdaptiveMaxMultiplier)));
            if (!(settings.dynamicCadenceProbeIntervalSeconds >= Limits::lowestProbeIntervalSeconds
                  && settings.dynamicCadenceProbeIntervalSeconds <= Limits::highestProbeIntervalSeconds))
                return refused(problem,
                               between("dynamic_cadence_probe_interval_seconds",
                                       floatText(Limits::lowestProbeIntervalSeconds),
                                       floatText(Limits::highestProbeIntervalSeconds)));
            if (settings.flowScale < Limits::lowestFlowScale || settings.flowScale > Limits::highestFlowScale)
                return refused(problem, between("flow_scale", floatText(Limits::lowestFlowScale), floatText(Limits::highestFlowScale)));
            return true;
        }

        bool readSettings(const toml::table& table, MakoSettings& settings, std::string& problem)
        {
            for (const std::string_view key : {"frame_generation_model", "mako_fg_mode"})
                if (table.contains(key) && !table[key].is_string())
                    return refused(problem, std::string(key) + " must be a string");
            if (table.contains("gpu_headroom_percent"))
            {
                const std::optional<int64_t> headroom = table["gpu_headroom_percent"].value<int64_t>();
                if (!table["gpu_headroom_percent"].is_integer() || !headroom || *headroom < 0 || *headroom > 20 || *headroom % 5 != 0)
                    return refused(problem, headroomRefusal);
            }
            const MakoSettings         defaults;
            std::optional<std::string> choiceProblem;
            forEachSetting([&](std::string_view key, auto member) {
                using Value = std::remove_cvref_t<decltype(settings.*member)>;
                if constexpr (std::is_same_v<Value, std::optional<std::string>>)
                    settings.*member = table[key].template value<std::string>();
                else
                    settings.*member = table[key].value_or(defaults.*member);
                if constexpr (std::is_same_v<Value, std::string>)
                    if (!choiceProblem)
                        choiceProblem = choiceRefusal(key, settings.*member);
            });
            if (choiceProblem)
                return refused(problem, *choiceProblem);
            if (settings.gpu && settings.gpu->empty())
                settings.gpu.reset();
            return validated(settings, problem);
        }

        std::vector<std::string> activityOf(const toml::node_view<const toml::node>& node)
        {
            std::vector<std::string> activeIn;
            if (const std::optional<std::string> one = node.value<std::string>())
                activeIn.push_back(*one);
            if (const toml::array* many = node.as_array())
                for (const toml::node& entry : *many)
                    if (const std::optional<std::string> game = entry.value<std::string>())
                        activeIn.push_back(*game);
            return activeIn;
        }

        bool readProfile(const toml::table& base, MakoProfile& profile, std::string& problem)
        {
            profile.name     = base["name"].value_or<std::string>("unnamed");
            profile.activeIn = activityOf(base["active_in"]);
            if (!readSettings(base, profile.settings, problem))
                return false;
            if (!base.contains("handheld") && !base.contains("docked"))
                return true;
            MakoPowerSets sets;
            for (const auto& [mode, into] : {std::pair<std::string_view, MakoSettings*>{"handheld", &sets.handheld}, {"docked", &sets.docked}})
            {
                const toml::table* overrides = base[mode].as_table();
                if (overrides == nullptr)
                    return refused(problem, powerTablesRefusal);
                toml::table merged = base;
                merged.erase("handheld");
                merged.erase("docked");
                for (const auto& [key, value] : *overrides)
                {
                    if (key == "name" || key == "active_in" || key == "handheld" || key == "docked")
                        return refused(problem, sharedNamesRefusal);
                    merged.insert_or_assign(key, value);
                }
                if (!readSettings(merged, *into, problem))
                    return false;
            }
            profile.power = sets;
            return true;
        }

        void editSettings(TableEditor& editor, const MakoSettings& current, const MakoSettings& wanted, bool& clean)
        {
            forEachSetting([&](std::string_view key, auto member) {
                if (!sameValue(current.*member, wanted.*member))
                    editor.set(key, valueText(wanted.*member, false, clean), false);
            });
        }

        std::string newPowerSetText(std::string_view mode, const MakoSettings& base, const MakoSettings& wanted, bool& clean)
        {
            std::string out = "[profile." + std::string(mode) + "]\n";
            forEachSetting([&](std::string_view key, auto member) {
                if (!sameValue(base.*member, wanted.*member))
                    out += std::string(key) + " = " + valueText(wanted.*member, true, clean).value_or("") + "\n";
            });
            return out;
        }

        std::string newProfileText(const MakoProfile& profile, bool& clean)
        {
            std::string out = "\n[[profile]]\nname = " + optionalString(profile.name, clean).value_or("") + "\n";
            if (!profile.activeIn.empty())
                out += "active_in = " + activeInText(profile.activeIn, clean).value_or("") + "\n";
            const MakoSettings defaults;
            forEachSetting([&](std::string_view key, auto member) {
                if (!sameValue(defaults.*member, profile.settings.*member))
                    out += std::string(key) + " = " + valueText(profile.settings.*member, false, clean).value_or("") + "\n";
            });
            if (profile.power)
                out += newPowerSetText("handheld", profile.settings, profile.power->handheld, clean)
                       + newPowerSetText("docked", profile.settings, profile.power->docked, clean);
            return out;
        }

        bool editPowerSet(const Positions&       positions,
                          const toml::table&     overrides,
                          const MakoSettings&    current,
                          const MakoSettings&    base,
                          const MakoSettings&    wanted,
                          std::vector<TextEdit>& edits,
                          bool&                  clean)
        {
            std::vector<TextEdit> own;
            TableEditor           editor{positions, overrides, own, ""};
            forEachSetting([&](std::string_view key, auto member) {
                const bool same = overrides.contains(key) ? sameValue(current.*member, wanted.*member) : sameValue(base.*member, wanted.*member);
                if (!same)
                    editor.set(key, valueText(wanted.*member, true, clean), false);
            });
            editor.finish();
            if (own.empty())
                return true;
            if (!opensWith(positions, overrides, "["))
                return false;
            edits.insert(edits.end(), own.begin(), own.end());
            return true;
        }

        bool editProfile(std::string& text, const MakoProfile& wanted, std::string& problem)
        {
            std::string                   refusal;
            const std::optional<MakoFile> file = parseMakoFile(text, refusal);
            if (!file)
                return refused(problem, "MAKO refuses its file: " + refusal);
            const std::optional<toml::table> table = tomlOf(text, problem);
            if (!table)
                return false;
            const auto at = std::find_if(file->profiles.begin(), file->profiles.end(), [&](const MakoProfile& p) { return p.name == wanted.name; });
            bool       clean = true;
            if (at == file->profiles.end())
            {
                const std::string block = newProfileText(wanted, clean);
                if (!clean)
                    return refused(problem, "the profile \"" + wanted.name + "\" cannot be written into MAKO's file");
                text += (text.empty() || text.back() == '\n' ? "" : "\n") + block;
                return true;
            }
            const MakoProfile& current = *at;
            const toml::table& found   = *(*table)["profile"][static_cast<size_t>(at - file->profiles.begin())].as_table();
            const Positions    positions(text);
            const std::string  cannotChange = "MAKO's profile '" + wanted.name + "' ";
            if (!opensWith(positions, found, "[["))
                return refused(problem, cannotChange + "is not a [[profile]] section the overlay can change");
            std::vector<TextEdit> edits;
            TableEditor           editor{positions, found, edits, ""};
            if (current.activeIn != wanted.activeIn)
                editor.set("active_in", activeInText(wanted.activeIn, clean), false);
            editSettings(editor, current.settings, wanted.settings, clean);
            const std::array<std::string_view, 2> modes = {"handheld", "docked"};
            if (current.power && wanted.power)
            {
                const std::array<std::pair<const MakoSettings*, const MakoSettings*>, 2> sets = {
                    {{&current.power->handheld, &wanted.power->handheld}, {&current.power->docked, &wanted.power->docked}}};
                for (size_t i = 0; i < modes.size(); i++)
                    if (!editPowerSet(positions, *found[modes[i]].as_table(), *sets[i].first, wanted.settings, *sets[i].second, edits, clean))
                        return refused(problem,
                                       cannotChange + "has a " + std::string(modes[i]) + " table that is not a [profile." + std::string(modes[i])
                                           + "] section the overlay can change");
            }
            else if (wanted.power)
                editor.appended += newPowerSetText("handheld", wanted.settings, wanted.power->handheld, clean)
                                   + newPowerSetText("docked", wanted.settings, wanted.power->docked, clean);
            else if (current.power)
                for (const std::string_view mode : modes)
                {
                    const toml::table& overrides = *found[mode].as_table();
                    if (!opensWith(positions, overrides, "["))
                        return refused(problem,
                                       cannotChange + "has a " + std::string(mode) + " table that is not a [profile." + std::string(mode)
                                           + "] section the overlay can remove");
                    removeSection(positions, overrides, edits);
                }
            editor.finish();
            if (!clean)
                return refused(problem, "a game name or GPU name for MAKO is not clean UTF-8 or holds a control character");
            text = applied(text, edits);
            return true;
        }

        bool editGlobal(std::string& text, const MakoGlobal& wanted, std::string& problem)
        {
            std::string                   refusal;
            const std::optional<MakoFile> file = parseMakoFile(text, refusal);
            if (!file)
                return refused(problem, "MAKO refuses its file: " + refusal);
            if (file->global == wanted)
                return true;
            const std::optional<toml::table> table = tomlOf(text, problem);
            if (!table)
                return false;
            const Positions       positions(text);
            const MakoGlobal&     current = file->global;
            bool                  clean   = true;
            std::vector<TextEdit> edits;
            if (!table->contains("global"))
            {
                const MakoGlobal defaults;
                std::string      block = "\n[global]\n";
                if (wanted.dll != defaults.dll)
                    block += "dll = " + optionalString(wanted.dll, clean).value_or("") + "\n";
                if (wanted.allowHalfPrecision != defaults.allowHalfPrecision)
                    block += std::string("allow_fp16 = ") + (wanted.allowHalfPrecision ? "true" : "false") + "\n";
                edits.push_back({text.size(), text.size(), (text.empty() || text.back() == '\n' ? "" : "\n") + block});
            }
            else if (const toml::table* global = (*table)["global"].as_table(); global && opensWith(positions, *global, "["))
            {
                TableEditor editor{positions, *global, edits, ""};
                editor.set("dll", optionalString(wanted.dll, clean), current.dll == wanted.dll);
                editor.set("allow_fp16", wanted.allowHalfPrecision ? "true" : "false", current.allowHalfPrecision == wanted.allowHalfPrecision);
                editor.finish();
            }
            else
                return refused(problem, "MAKO's file has a global entry that is not a [global] section the overlay can change");
            if (!clean)
                return refused(problem, "a path for MAKO is not clean UTF-8 or holds a control character");
            text = applied(text, edits);
            return true;
        }

        void readMakoEnvironment(MakoProcess& process)
        {
            process.profileNamed         = nonEmptyEnvironment("MAKO_PROFILE");
            process.fallbackProfile      = nonEmptyEnvironment("MAKO_PROFILE_FALLBACK");
            const char* follow           = std::getenv("MAKO_FOLLOW_CURRENT_PROFILE");
            process.followCurrentProfile = follow != nullptr && std::string_view(follow) == "1";
        }

        bool leftAloneByMako(const MakoProcess& process)
        {
            std::string_view executable = process.wineExecutable && !process.wineExecutable->empty() ? *process.wineExecutable : process.executable;
            if (const size_t separator = executable.find_last_of("/\\"); separator != std::string_view::npos)
                executable.remove_prefix(separator + 1);
            return std::any_of(launchersMakoLeavesAlone.begin(), launchersMakoLeavesAlone.end(), [&](std::string_view launcher) {
                return asciiEqual(executable, launcher);
            });
        }

        std::string firstLine(const fs::path& path)
        {
            std::ifstream stream(path);
            std::string   line;
            std::getline(stream, line);
            return line;
        }
    }

    bool MakoSettings::operator==(const MakoSettings& other) const
    {
        bool same = true;
        forEachSetting([&](std::string_view, auto member) { same = same && sameValue(this->*member, other.*member); });
        return same;
    }

    std::optional<MakoFile> parseMakoFile(const std::string& text, std::string& problem)
    {
        toml::table table;
        try
        {
            table = toml::parse(text);
        }
        catch (const toml::parse_error& error)
        {
            problem = unparsedRefusal + std::string(error.description());
            return std::nullopt;
        }
        const toml::node_view<toml::node> version = table["version"];
        if (!version || !version.is_integer() || version.value<int64_t>() != 2)
        {
            problem = wrongVersionRefusal;
            return std::nullopt;
        }
        MakoFile file;
        file.currentProfile = table["current_profile"].value<std::string>();
        if (const toml::table* global = table["global"].as_table())
        {
            file.global.dll                = (*global)["dll"].value<std::string>();
            file.global.allowHalfPrecision = (*global)["allow_fp16"].value_or(true);
        }
        if (table["profile"].is_array_of_tables())
            for (const toml::node& entry : *table["profile"].as_array())
                if (!readProfile(*entry.as_table(), file.profiles.emplace_back(), problem))
                    return std::nullopt;
        return file;
    }

    MakoSettings makoSettingsInEffect(MakoSettings settings)
    {
        if (settings.dynamicCadenceRecovery)
        {
            settings.adaptiveAutoBaseFpsCap              = false;
            settings.adaptiveFractionalRealFramePriority = "auto";
            settings.baseFpsCap                          = 0;
            settings.gpuHeadroomPercent                  = 0;
        }
        return settings;
    }

    const MakoSettings& makoSettingsFor(const MakoProfile& profile, MakoPowerSource source)
    {
        if (!profile.power || source == MakoPowerSource::Unknown)
            return profile.settings;
        return source == MakoPowerSource::Handheld ? profile.power->handheld : profile.power->docked;
    }

    PowerSupplies readPowerSupplies(const std::filesystem::path& root)
    {
        PowerSupplies          supplies;
        std::error_code        error;
        fs::directory_iterator entries(root, error);
        if (error)
            return supplies;
        bool   powered    = false;
        bool   supply     = false;
        bool   incomplete = false;
        size_t count      = 0;
        for (const fs::directory_iterator end{}; entries != end; entries.increment(error))
        {
            if (error || ++count > 64)
                return supplies;
            const fs::path path = entries->path();
            if (firstLine(path / "scope") == "Device")
                continue;
            const std::string type = firstLine(path / "type");
            if (type == "Battery")
                supplies.battery = supplies.battery || firstLine(path / "present") != "0";
            else if (std::find(supplyTypes.begin(), supplyTypes.end(), type) != supplyTypes.end())
            {
                supply                   = true;
                const std::string online = firstLine(path / "online");
                powered                  = powered || online == "1";
                incomplete               = incomplete || online != "0";
            }
            else if (type.empty())
                incomplete = true;
        }
        if (error)
            return supplies;
        if (powered)
            supplies.source = MakoPowerSource::Docked;
        else if (supplies.battery && supply && !incomplete)
            supplies.source = MakoPowerSource::Handheld;
        return supplies;
    }

    std::optional<std::string>
    makoTextWith(const std::string& text, const std::optional<MakoGlobal>& global, const std::optional<MakoProfile>& profile, std::string& problem)
    {
        std::string out = text.empty() ? emptyMakoText : text;
        if (global && !editGlobal(out, *global, problem))
            return std::nullopt;
        if (profile && !editProfile(out, *profile, problem))
            return std::nullopt;
        std::string                   refusal;
        const std::optional<MakoFile> back = parseMakoFile(out, refusal);
        if (!back)
        {
            problem = "MAKO would refuse its file after this change: " + refusal;
            return std::nullopt;
        }
        if ((global && back->global != *global)
            || (profile && (!makoProfileNamed(*back, profile->name) || *makoProfileNamed(*back, profile->name) != *profile)))
        {
            problem = "the change did not read back from MAKO's file as it was written";
            return std::nullopt;
        }
        return out;
    }

    std::string makoConfigPath()
    {
        if (const std::optional<std::string> named = nonEmptyEnvironment("MAKO_CONFIG"))
            return *named;
        if (const std::optional<std::string> configHome = nonEmptyEnvironment("XDG_CONFIG_HOME"))
            return (fs::path(*configHome) / "mako-render" / "conf.toml").string();
        if (const std::optional<std::string> home = nonEmptyEnvironment("HOME"))
            return (fs::path(*home) / ".config" / "mako-render" / "conf.toml").string();
        return "/etc/mako-render/conf.toml";
    }

    MakoRead readMakoFile(const std::string& path)
    {
        const FileText found = readFileText(path);
        MakoRead       read{found.exists, found.text, std::nullopt, found.problem};
        if (found.problem.empty())
            read.file = found.exists ? parseMakoFile(read.text, read.problem) : MakoFile{};
        return read;
    }

    bool saveMakoFile(const std::string&                path,
                      const std::optional<MakoGlobal>&  global,
                      const std::optional<MakoProfile>& profile,
                      MakoFile&                         written,
                      std::string&                      problem)
    {
        const MakoRead read = readMakoFile(path);
        if (!read.file)
            return refused(problem, read.exists ? "MAKO refuses " + path + ": " + read.problem : read.problem);
        const std::optional<std::string> text = makoTextWith(read.text, global, profile, problem);
        if (!text)
            return false;
        if (read.exists && *text == read.text)
        {
            written = *read.file;
            return true;
        }
        if (!replaceFileAtomically(path, *text, makoFile, makoWriteRules, problem))
            return false;
        std::string unused;
        written = *parseMakoFile(*text, unused);
        return true;
    }

    const MakoProfile* makoProfileNamed(const MakoFile& file, const std::string& name)
    {
        const auto at = std::find_if(file.profiles.begin(), file.profiles.end(), [&](const MakoProfile& p) { return p.name == name; });
        return at == file.profiles.end() ? nullptr : &*at;
    }

    std::optional<size_t> makoProfileFor(const MakoFile& file, const MakoProcess& process)
    {
        if (leftAloneByMako(process))
            return std::nullopt;
        const auto named = [&](const std::string& name) -> std::optional<size_t> {
            if (const MakoProfile* profile = makoProfileNamed(file, name))
                return static_cast<size_t>(profile - file.profiles.data());
            return std::nullopt;
        };
        const auto first = [&](const auto& matches) -> std::optional<size_t> {
            for (size_t i = 0; i < file.profiles.size(); i++)
                for (const std::string& active : file.profiles[i].activeIn)
                    if (matches(active))
                        return i;
            return std::nullopt;
        };
        if (process.followCurrentProfile && file.currentProfile)
            if (const std::optional<size_t> current = named(*file.currentProfile))
                return current;
        if (process.profileNamed)
            if (const std::optional<size_t> chosen = named(*process.profileNamed))
                return chosen;
        if (const auto byExecutable = first([&](const std::string& active) { return process.executable.ends_with(active); }))
            return byExecutable;
        if (process.wineExecutable)
            if (const auto byWineExecutable = first([&](const std::string& active) { return process.wineExecutable->ends_with(active); }))
                return byWineExecutable;
        if (!process.processName.empty())
            if (const auto byName = first([&](const std::string& active) { return process.processName == active; }))
                return byName;
        if (process.fallbackProfile)
            return named(*process.fallbackProfile);
        return std::nullopt;
    }

    MakoProcess thisMakoProcess()
    {
        MakoProcess     process;
        std::error_code unreadable;
        process.executable = fs::read_symlink("/proc/self/exe", unreadable).string();
        if (process.executable.find("wine") != std::string::npos || process.executable.find("proton") != std::string::npos)
        {
            std::ifstream maps("/proc/self/maps");
            std::string   line;
            while (std::getline(maps, line))
            {
                if (!namesAnExe(line))
                    continue;
                size_t begin = line.find_first_of('/');
                if (begin == std::string::npos)
                    begin = line.find_last_of(' ');
                if (begin == std::string::npos)
                    continue;
                process.wineExecutable = line.substr(begin);
                break;
            }
        }
        std::ifstream comm("/proc/self/comm");
        std::getline(comm, process.processName);
        readMakoEnvironment(process);
        return process;
    }

    MakoProcess makoProcessOf(const std::vector<std::string>& command, const std::string& program)
    {
        MakoProcess process;
        const bool  windows = namesAnExe(program);
        for (const std::string& word : command)
        {
            const bool named =
                word.ends_with(program)
                && (word.size() == program.size() || word[word.size() - program.size() - 1] == '/' || word[word.size() - program.size() - 1] == '\\');
            if (named && windows && !process.wineExecutable)
                process.wineExecutable = word;
            else if (named && !windows)
                process.executable = word;
        }
        process.processName = program.substr(0, 15);
        readMakoEnvironment(process);
        return process;
    }

    std::vector<std::string> makoActiveIn(const std::vector<std::string>& games)
    {
        return activeInEntries(games, makoEntryFor);
    }

    MakoProfile newMakoProfileFor(const std::string& profileName, const std::vector<std::string>& games)
    {
        MakoProfile profile;
        profile.name                            = frameGenerationProfileNameOf(profileName);
        profile.activeIn                        = makoActiveIn(games);
        profile.settings.frameGenerationEnabled = false;
        return profile;
    }

    std::vector<MakoProfile> makoProfilesToSync(const MakoFile& file, const std::map<std::string, std::vector<std::string>>& gamesByProfile)
    {
        MakoFile ownersOwn;
        for (const MakoProfile& profile : file.profiles)
            if (!madeByVkDaedalus(profile.name))
                ownersOwn.profiles.push_back(profile);
        const auto ownedElsewhere = [&](const std::string& game) {
            MakoProcess process;
            if (namesAnExe(game))
                process.wineExecutable = makoEntryFor(game);
            else
                process.executable = makoEntryFor(game);
            process.processName = game.substr(0, 15);
            return makoProfileFor(ownersOwn, process).has_value();
        };
        std::vector<MakoProfile> changed;
        for (const MakoProfile& profile : file.profiles)
        {
            if (!madeByVkDaedalus(profile.name))
                continue;
            const std::vector<std::string> wanted = syncedActiveIn(profile.name, profile.activeIn, gamesByProfile, makoEntryFor, ownedElsewhere);
            if (wanted != profile.activeIn)
            {
                MakoProfile next = profile;
                next.activeIn    = wanted;
                changed.push_back(next);
            }
        }
        return changed;
    }

    std::optional<std::string> makoTextRenamed(const std::string& text, const std::string& from, const std::string& to, std::string& problem)
    {
        std::string out = text;
        if (!renameProfileIn(out, from, to, "MAKO", problem))
            return std::nullopt;
        std::string                   refusal;
        const std::optional<MakoFile> back = parseMakoFile(out, refusal);
        if (!back)
        {
            problem = "MAKO would refuse its file after this change: " + refusal;
            return std::nullopt;
        }
        if (out != text && (makoProfileNamed(*back, from) || !makoProfileNamed(*back, to)))
        {
            problem = "the rename did not read back from MAKO's file as it was written";
            return std::nullopt;
        }
        return out;
    }

    bool syncMakoActiveIn(const std::string& path, const std::map<std::string, std::vector<std::string>>& gamesByProfile, std::string& problem)
    {
        const MakoRead read = readMakoFile(path);
        if (!read.exists)
            return read.problem.empty() || refused(problem, read.problem);
        if (!read.file)
            return refused(problem, "MAKO refuses " + path + ": " + read.problem);
        std::string text = read.text;
        for (const MakoProfile& profile : makoProfilesToSync(*read.file, gamesByProfile))
        {
            const std::optional<std::string> next = makoTextWith(text, std::nullopt, profile, problem);
            if (!next)
                return false;
            text = *next;
        }
        return text == read.text || replaceFileAtomically(path, text, makoFile, makoWriteRules, problem);
    }

    bool renameMakoProfile(const std::string& path, const std::string& from, const std::string& to, std::string& problem)
    {
        const MakoRead read = readMakoFile(path);
        if (!read.exists)
            return read.problem.empty() || refused(problem, read.problem);
        if (!read.file)
            return refused(problem, "MAKO refuses " + path + ": " + read.problem);
        const std::optional<std::string> text = makoTextRenamed(read.text, from, to, problem);
        if (!text)
            return false;
        return *text == read.text || replaceFileAtomically(path, *text, makoFile, makoWriteRules, problem);
    }

    std::optional<std::string> makoDllPath(const MakoGlobal& global)
    {
        if (global.dll)
            return *global.dll;
        if (const std::optional<std::string> data = nonEmptyEnvironment("XDG_DATA_HOME"))
            if (const fs::path path = fs::path(*data) / "Steam/steamapps/common" / makoDllUnderCommon; isRegularFile(path))
                return path.string();
        if (const std::optional<std::string> home = nonEmptyEnvironment("HOME"))
            for (const std::string_view folder : steamCommonFolders)
                if (const fs::path path = fs::path(*home) / folder / makoDllUnderCommon; isRegularFile(path))
                    return path.string();
        std::error_code noDirectory;
        const fs::path  here = fs::current_path(noDirectory) / "Lossless.dll";
        if (!noDirectory && isRegularFile(here))
            return here.string();
        return std::nullopt;
    }

    DllStatus makoDllStatus(const MakoGlobal& global)
    {
        return dllStatusAt(makoDllPath(global), makoDllUnderCommon);
    }

    bool makoLayerLoaded()
    {
        static const bool loaded = libraryMapped(makoLayerLibrary);
        return loaded;
    }

    std::optional<bool> makoLayerListedBy(PFN_vkEnumerateInstanceLayerProperties enumerate)
    {
        return layerListedBy(enumerate, makoLayerName);
    }

    std::optional<bool> makoLayerVisible()
    {
        static const std::optional<bool> visible = layerVisible(makoLayerName);
        return visible;
    }

    bool makoSettingsFromEnvironment()
    {
        return std::getenv("MAKO_ENV") != nullptr;
    }

    std::string_view loadedFrameGenerationLayerName()
    {
        return makoLayerLoaded() ? "MAKO" : "lsfg-vk";
    }
}
