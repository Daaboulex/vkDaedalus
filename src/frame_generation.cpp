#include "frame_generation.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>

#include <dlfcn.h>

#include <toml++/toml.hpp>

namespace vkBasalt
{
    namespace
    {
        namespace fs = std::filesystem;

        const std::string ownProfilePrefix = "vkdaedalus ";
        const std::string emptyLsfgText    = "version = 2\n\n[global]\n";

        std::string floatText(float value)
        {
            char        text[32];
            const auto  written = std::to_chars(text, text + sizeof(text), value);
            std::string out(text, written.ptr);
            if (out.find_first_of(".e") == std::string::npos)
                out += ".0";
            return out;
        }

        bool validUtf8(const std::string& text)
        {
            size_t i = 0;
            while (i < text.size())
            {
                const unsigned char lead = static_cast<unsigned char>(text[i]);
                size_t              more = 0;
                if ((lead >> 5) == 0x6)
                    more = 1;
                else if ((lead >> 4) == 0xe)
                    more = 2;
                else if ((lead >> 3) == 0x1e)
                    more = 3;
                else if (lead >= 0x80)
                    return false;
                if (text.size() - i <= more)
                    return false;
                for (size_t k = 1; k <= more; k++)
                    if ((static_cast<unsigned char>(text[i + k]) >> 6) != 0x2)
                        return false;
                i += more + 1;
            }
            return true;
        }

        std::optional<std::string> quoted(const std::string& text)
        {
            if (!validUtf8(text))
                return std::nullopt;
            std::string out = "\"";
            for (const char c : text)
            {
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
                    return std::nullopt;
                if (c == '"' || c == '\\')
                    out += '\\';
                out += c;
            }
            return out + "\"";
        }

        std::string lowered(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        template<size_t N>
        bool isChoice(const std::array<LsfgChoice, N>& choices, const std::string& name)
        {
            return std::any_of(choices.begin(), choices.end(), [&](const LsfgChoice& choice) { return choice.name == name; });
        }

        std::string activeIn(const std::string& game)
        {
            const std::string tail             = game.size() >= 4 ? lowered(game.substr(game.size() - 4)) : "";
            const bool        exeInAnotherCase = tail == ".exe" && game.compare(game.size() - 4, 4, ".exe") != 0;
            return exeInAnotherCase ? game : "/" + game;
        }

        bool refused(std::string& problem, std::string why)
        {
            problem = std::move(why);
            return false;
        }

        std::string typeProblem(const toml::key& key)
        {
            return "Invalid type for value '" + std::string(key.str()) + "'";
        }

        bool readGlobal(const toml::table& table, FrameGenerationGlobal& global, std::string& problem)
        {
            for (const auto& [key, value] : table)
            {
                if (key == "allow_fp16")
                {
                    const std::optional<bool> allowed = value.value<bool>();
                    if (!allowed)
                        return refused(problem, typeProblem(key));
                    global.allowHalfPrecision = *allowed;
                }
                else if (key == "dll" || key == "log_file" || key == "log_level")
                {
                    const std::optional<std::string> text = value.value<std::string>();
                    if (!text)
                        return refused(problem, typeProblem(key));
                    if (key == "dll")
                        global.dll = *text;
                    else if (key == "log_file")
                        global.logFile = *text;
                    else if (isChoice(lsfgLogLevels, lowered(*text)))
                        global.logLevel = lowered(*text);
                    else
                        return refused(problem, "Invalid enum name: " + lowered(*text));
                }
                else
                    return refused(problem, "Unknown key in [global] section: " + std::string(key.str()));
            }
            return true;
        }

        bool readProfile(const toml::table& table, FrameGenerationProfile& profile, std::string& problem)
        {
            for (const auto& [key, value] : table)
            {
                const auto choice = [&](const auto& choices, std::optional<std::string>& into) {
                    const std::optional<std::string> text = value.value<std::string>();
                    if (!text)
                        return refused(problem, typeProblem(key));
                    if (!isChoice(choices, lowered(*text)))
                        return refused(problem, "Invalid enum name: " + lowered(*text));
                    into = lowered(*text);
                    return true;
                };
                const auto flag = [&](bool& into) {
                    const std::optional<bool> set = value.value<bool>();
                    if (!set)
                        return refused(problem, typeProblem(key));
                    into = *set;
                    return true;
                };
                if (key == "name")
                {
                    const std::optional<std::string> name = value.value<std::string>();
                    if (!name)
                        return refused(problem, typeProblem(key));
                    profile.name = *name;
                }
                else if (key == "active_in")
                {
                    if (value.is_string())
                        profile.activeIn.push_back(*value.value<std::string>());
                    else if (const toml::array* entries = value.as_array())
                        for (const toml::node& entry : *entries)
                        {
                            const std::optional<std::string> game = entry.value<std::string>();
                            if (!game)
                                return refused(problem, typeProblem(key));
                            profile.activeIn.push_back(*game);
                        }
                    else
                        return refused(problem, "Invalid type for active_in");
                }
                else if (key == "multiplier")
                {
                    const std::optional<uint32_t> multiplier = value.value<uint32_t>();
                    if (!multiplier)
                        return refused(problem, typeProblem(key));
                    profile.multiplier = *multiplier;
                }
                else if (key == "flow_scale")
                {
                    const std::optional<float> flowScale = value.value<float>();
                    if (!flowScale)
                        return refused(problem, typeProblem(key));
                    profile.flowScale = *flowScale;
                }
                else if (key == "performance_mode")
                {
                    if (!flag(profile.performanceMode))
                        return false;
                }
                else if (key == "color_space")
                {
                    if (!choice(lsfgColorSpaces, profile.colorSpace))
                        return false;
                }
                else if (key == "transfer_function")
                {
                    if (!choice(lsfgTransferFunctions, profile.transferFunction))
                        return false;
                }
                else if (key == "pacing_mode" || key == "pacing")
                {
                    const std::optional<std::string> pacing = value.value<std::string>();
                    if (!pacing)
                        return refused(problem, typeProblem(key));
                    if (lowered(*pacing) != "vsync")
                        return refused(problem, "Invalid enum name: " + lowered(*pacing));
                }
                else if (key == "override_present_mode")
                {
                    if (!flag(profile.overridePresentMode))
                        return false;
                }
                else if (key == "preserve_swapchain_image_count")
                {
                    if (!flag(profile.preserveSwapchainImageCount))
                        return false;
                }
                else
                    return refused(problem, "Unknown key in profile section: " + std::string(key.str()));
            }
            if (profile.multiplier < 1)
                return refused(problem, "Profile '" + profile.name + "' has multiplier < 1");
            if (profile.flowScale < lowestFlowScale || profile.flowScale > highestFlowScale)
                return refused(problem, "Profile '" + profile.name + "' has flow_scale out of range (must be between 0.25 and 1.0)");
            return true;
        }

        bool readFile(const toml::table& table, LsfgFile& file, std::string& problem)
        {
            int64_t version = 0;
            for (const auto& [key, value] : table)
            {
                if (key == "version")
                {
                    const std::optional<int64_t> number = value.value<int64_t>();
                    if (!number)
                        return refused(problem, typeProblem(key));
                    version = *number;
                }
                else if (key != "global" && key != "profile")
                    return refused(problem, "Unknown key in configuration: " + std::string(key.str()));
            }
            if (version != 2)
                return refused(problem, "Unsupported configuration version (must be 2).");
            const toml::table* global = table["global"].as_table();
            if (global == nullptr)
                return refused(problem, "Invalid global section in configuration");
            if (!readGlobal(*global, file.global, problem))
                return false;
            if (!table["profile"].is_array_of_tables())
                return refused(problem, "Invalid profiles section in configuration");
            for (const toml::node& entry : *table["profile"].as_array())
                if (!readProfile(*entry.as_table(), file.profiles.emplace_back(), problem))
                    return false;
            return true;
        }

        std::optional<toml::table> tomlOf(const std::string& text, std::string& problem)
        {
            try
            {
                return toml::parse(text);
            }
            catch (const toml::parse_error& error)
            {
                const toml::source_position& at = error.source().begin;
                problem = std::string(error.description()) + " at line " + std::to_string(at.line) + ", column " + std::to_string(at.column);
                return std::nullopt;
            }
        }

        struct TextEdit
        {
            size_t      from = 0;
            size_t      to   = 0;
            std::string with;
        };

        struct Positions
        {
            const std::string&  text;
            std::vector<size_t> lineStarts;

            explicit Positions(const std::string& text) : text(text), lineStarts{0}
            {
                for (size_t i = 0; i < text.size(); i++)
                    if (text[i] == '\n')
                        lineStarts.push_back(i + 1);
            }

            size_t at(const toml::source_position& position) const
            {
                size_t offset = lineStarts[std::clamp<size_t>(position.line, 1, lineStarts.size()) - 1];
                for (uint32_t column = 1; column < position.column && offset < text.size(); column++)
                {
                    offset++;
                    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80)
                        offset++;
                }
                return offset;
            }

            size_t lineStart(size_t offset) const
            {
                const size_t newline = offset == 0 ? std::string::npos : text.rfind('\n', offset - 1);
                return newline == std::string::npos ? 0 : newline + 1;
            }

            size_t afterLine(size_t offset) const
            {
                const size_t newline = text.find('\n', offset);
                return newline == std::string::npos ? text.size() : newline + 1;
            }
        };

        struct TableEditor
        {
            const Positions&       positions;
            const toml::table&     table;
            std::vector<TextEdit>& edits;
            std::string            appended;

            void set(std::string_view key, const std::optional<std::string>& wanted, bool same)
            {
                if (same)
                    return;
                const toml::node* present = table.get(key);
                if (present == nullptr)
                {
                    if (wanted)
                        appended += std::string(key) + " = " + *wanted + "\n";
                    return;
                }
                const size_t from = positions.at(present->source().begin);
                const size_t to   = positions.at(present->source().end);
                if (wanted)
                    edits.push_back({from, to, *wanted});
                else
                    edits.push_back({positions.lineStart(from), positions.afterLine(to), ""});
            }

            void finish()
            {
                if (appended.empty())
                    return;
                toml::source_position last = table.source().end;
                for (const auto& [_, value] : table)
                    if (value.source().end.line > last.line || (value.source().end.line == last.line && value.source().end.column > last.column))
                        last = value.source().end;
                const size_t at = positions.afterLine(positions.at(last));
                const bool   needsNewline = at == positions.text.size() && !positions.text.empty() && positions.text.back() != '\n';
                edits.push_back({at, at, (needsNewline ? "\n" : "") + appended});
            }
        };

        bool opensWith(const Positions& positions, const toml::table& table, std::string_view header)
        {
            return positions.text.compare(positions.at(table.source().begin), header.size(), header) == 0;
        }

        std::string applied(std::string text, std::vector<TextEdit> edits)
        {
            std::stable_sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return a.from > b.from; });
            for (const TextEdit& edit : edits)
                text.replace(edit.from, edit.to - edit.from, edit.with);
            return text;
        }

        std::optional<std::string> optionalString(const std::optional<std::string>& value, bool& clean)
        {
            if (!value)
                return std::nullopt;
            const std::optional<std::string> text = quoted(*value);
            clean     = clean && text.has_value();
            return text;
        }

        bool editGlobal(std::string& text, const FrameGenerationGlobal& wanted, std::string& problem)
        {
            const std::optional<toml::table> table = tomlOf(text, problem);
            if (!table)
                return false;
            const Positions    positions(text);
            const toml::table* global = (*table)["global"].as_table();
            if (global == nullptr || !opensWith(positions, *global, "["))
                return refused(problem, "lsfg-vk's file has no [global] section to change");
            FrameGenerationGlobal current;
            if (!readGlobal(*global, current, problem))
                return false;
            bool                  clean = true;
            std::vector<TextEdit> edits;
            TableEditor           editor{positions, *global, edits, ""};
            editor.set("dll", optionalString(wanted.dll, clean), current.dll == wanted.dll);
            editor.set("allow_fp16", wanted.allowHalfPrecision ? "true" : "false", current.allowHalfPrecision == wanted.allowHalfPrecision);
            editor.set("log_level", optionalString(wanted.logLevel, clean), current.logLevel == wanted.logLevel);
            editor.set("log_file", optionalString(wanted.logFile, clean), current.logFile == wanted.logFile);
            editor.finish();
            if (!clean)
                return refused(problem, "a path for lsfg-vk is not clean UTF-8 or holds a control character");
            text = applied(text, edits);
            return true;
        }

        std::optional<std::string> activeInText(const std::vector<std::string>& games, bool& clean)
        {
            if (games.size() == 1)
                return optionalString(games.front(), clean);
            std::string out = "[";
            for (size_t i = 0; i < games.size(); i++)
                out += (i ? ", " : "") + optionalString(games[i], clean).value_or("");
            return out + "]";
        }

        std::string newProfileText(const FrameGenerationProfile& profile, bool& clean)
        {
            std::string out = "\n[[profile]]\nname = " + optionalString(profile.name, clean).value_or("") + "\n";
            if (!profile.activeIn.empty())
                out += "active_in = " + activeInText(profile.activeIn, clean).value_or("") + "\n";
            out += "multiplier = " + std::to_string(profile.multiplier) + "\nflow_scale = " + floatText(profile.flowScale) +
                   "\nperformance_mode = " + (profile.performanceMode ? "true" : "false") + "\n";
            if (profile.colorSpace)
                out += "color_space = " + optionalString(profile.colorSpace, clean).value_or("") + "\n";
            if (profile.transferFunction)
                out += "transfer_function = " + optionalString(profile.transferFunction, clean).value_or("") + "\n";
            out += std::string("override_present_mode = ") + (profile.overridePresentMode ? "true" : "false") +
                   "\npreserve_swapchain_image_count = " + (profile.preserveSwapchainImageCount ? "true" : "false") + "\n";
            return out;
        }

        bool editProfile(std::string& text, const FrameGenerationProfile& wanted, std::string& problem)
        {
            const std::optional<toml::table> table = tomlOf(text, problem);
            if (!table)
                return false;
            const Positions    positions(text);
            const toml::table* found = nullptr;
            if (const toml::array* profiles = (*table)["profile"].as_array())
                for (const toml::node& entry : *profiles)
                    if (const toml::table* candidate = entry.as_table(); candidate && candidate->get("name") &&
                                                                         candidate->get("name")->value<std::string>() == wanted.name)
                    {
                        found = candidate;
                        break;
                    }
            bool clean = true;
            if (found == nullptr)
            {
                const std::string block = newProfileText(wanted, clean);
                if (!clean)
                    return refused(problem, "the game name \"" + wanted.name + "\" cannot be written into lsfg-vk's file");
                text += (text.empty() || text.back() == '\n' ? "" : "\n") + block;
                return true;
            }
            if (!opensWith(positions, *found, "[["))
                return refused(problem, "lsfg-vk's profile '" + wanted.name + "' is not a [[profile]] section the overlay can change");
            FrameGenerationProfile current;
            if (!readProfile(*found, current, problem))
                return false;
            std::vector<TextEdit> edits;
            TableEditor           editor{positions, *found, edits, ""};
            editor.set("active_in", activeInText(wanted.activeIn, clean), current.activeIn == wanted.activeIn);
            editor.set("multiplier", std::to_string(wanted.multiplier), current.multiplier == wanted.multiplier);
            editor.set("flow_scale", floatText(wanted.flowScale), current.flowScale == wanted.flowScale);
            editor.set("performance_mode", wanted.performanceMode ? "true" : "false", current.performanceMode == wanted.performanceMode);
            editor.set("color_space", optionalString(wanted.colorSpace, clean), current.colorSpace == wanted.colorSpace);
            editor.set("transfer_function", optionalString(wanted.transferFunction, clean), current.transferFunction == wanted.transferFunction);
            editor.set("override_present_mode", wanted.overridePresentMode ? "true" : "false",
                       current.overridePresentMode == wanted.overridePresentMode);
            editor.set("preserve_swapchain_image_count", wanted.preserveSwapchainImageCount ? "true" : "false",
                       current.preserveSwapchainImageCount == wanted.preserveSwapchainImageCount);
            editor.finish();
            if (!clean)
                return refused(problem, "a game name for lsfg-vk is not clean UTF-8 or holds a control character");
            text = applied(text, edits);
            return true;
        }

        bool renameProfile(std::string& text, const std::string& from, const std::string& to, std::string& problem)
        {
            const std::optional<toml::table> table = tomlOf(text, problem);
            if (!table)
                return false;
            const Positions    positions(text);
            const toml::table* found = nullptr;
            if (const toml::array* profiles = (*table)["profile"].as_array())
                for (const toml::node& entry : *profiles)
                    if (const toml::table* candidate = entry.as_table(); candidate && candidate->get("name"))
                    {
                        const std::optional<std::string> name = candidate->get("name")->value<std::string>();
                        if (name == to)
                            return refused(problem, "lsfg-vk already has a profile named '" + to + "'");
                        if (name == from)
                            found = candidate;
                    }
            if (found == nullptr)
                return true;
            if (!opensWith(positions, *found, "[["))
                return refused(problem, "lsfg-vk's profile '" + from + "' is not a [[profile]] section the overlay can change");
            bool                  clean = true;
            std::vector<TextEdit> edits;
            TableEditor           editor{positions, *found, edits, ""};
            editor.set("name", optionalString(to, clean), false);
            editor.finish();
            if (!clean)
                return refused(problem, "the name \"" + to + "\" cannot be written into lsfg-vk's file");
            text = applied(text, edits);
            return true;
        }

        bool endsWith(const std::string& text, const std::string& tail)
        {
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        }

        std::optional<std::string> environment(const char* name)
        {
            const char* value = std::getenv(name);
            if (value == nullptr || *value == '\0')
                return std::nullopt;
            return std::string(value);
        }

        fs::path expandHome(const std::string& path)
        {
            const std::optional<std::string> home = environment("HOME");
            if (!path.starts_with('~') || !home)
                return path;
            return *home + path.substr(1);
        }

        fs::path fixDllPath(const fs::path& path)
        {
            const std::string file   = path.filename().string();
            const std::string parent = path.parent_path().filename().string();
            if (file == "Lossless Scaling")
                return path / "lsfg-vk.dll";
            if (file == "Lossless.dll" || file == "LosslessScaling.dll" || (file.empty() && parent == "Lossless Scaling"))
                return path.parent_path() / "lsfg-vk.dll";
            return path;
        }

        bool isFile(const fs::path& path)
        {
            std::error_code unreadable;
            return fs::is_regular_file(path, unreadable);
        }

        std::string readWhole(const fs::path& path)
        {
            std::ifstream     file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }
    }

    std::optional<LsfgFile> parseLsfgFile(const std::string& text, std::string& problem)
    {
        const std::optional<toml::table> table = tomlOf(text, problem);
        LsfgFile                         file;
        if (!table || !readFile(*table, file, problem))
            return std::nullopt;
        return file;
    }

    const FrameGenerationProfile* profileNamed(const LsfgFile& file, const std::string& name)
    {
        const auto at = std::find_if(file.profiles.begin(), file.profiles.end(), [&](const FrameGenerationProfile& p) { return p.name == name; });
        return at == file.profiles.end() ? nullptr : &*at;
    }

    std::optional<std::string> lsfgTextWith(const std::string& text, const std::optional<FrameGenerationGlobal>& global,
                                            const std::optional<FrameGenerationProfile>& profile, std::string& problem)
    {
        std::string out = text.empty() ? emptyLsfgText : text;
        if (global && !editGlobal(out, *global, problem))
            return std::nullopt;
        if (profile && !editProfile(out, *profile, problem))
            return std::nullopt;
        std::string                   refusal;
        const std::optional<LsfgFile> back = parseLsfgFile(out, refusal);
        if (!back)
        {
            problem = "lsfg-vk would refuse its file after this change: " + refusal;
            return std::nullopt;
        }
        if ((global && back->global != *global) || (profile && (!profileNamed(*back, profile->name) || *profileNamed(*back, profile->name) != *profile)))
        {
            problem = "the change did not read back from lsfg-vk's file as it was written";
            return std::nullopt;
        }
        return out;
    }

    std::string lsfgConfigPath()
    {
        std::string path = "/etc/lsfg-vk/conf.toml";
        if (const std::optional<std::string> home = environment("HOME"))
            path = (fs::path(*home) / ".config" / "lsfg-vk" / "conf.toml").string();
        if (const std::optional<std::string> configHome = environment("XDG_CONFIG_HOME"); configHome && std::getenv("LSFGVK_FLATPAK") == nullptr)
            path = (fs::path(*configHome) / "lsfg-vk" / "conf.toml").string();
        if (const std::optional<std::string> named = environment("LSFGVK_CONFIG"))
            path = *named;
        return path;
    }

    LsfgRead readLsfgFile(const std::string& path)
    {
        LsfgRead        read;
        std::error_code unreadable;
        const bool      exists = fs::exists(path, unreadable);
        if (unreadable)
        {
            read.problem = "cannot check " + path + ": " + unreadable.message();
            return read;
        }
        if (!exists)
        {
            read.file = LsfgFile{};
            return read;
        }
        read.exists = true;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            read.problem = "cannot read " + path;
            return read;
        }
        std::stringstream text;
        text << file.rdbuf();
        read.text = text.str();
        read.file = parseLsfgFile(read.text, read.problem);
        return read;
    }

    bool writeLsfgText(const std::string& path, const std::string& text, std::string& problem)
    {
        std::error_code       error;
        const fs::file_status status = fs::symlink_status(path, error);
        if (status.type() == fs::file_type::symlink)
            return refused(problem, path + " is a link, and the overlay writes lsfg-vk's file only where it is a plain file");
        if (status.type() != fs::file_type::not_found && status.type() != fs::file_type::regular)
            return refused(problem, "cannot check " + path + (error ? ": " + error.message() : ": it is not a plain file"));
        if (const fs::path parent = fs::path(path).parent_path(); !parent.empty())
        {
            fs::create_directories(parent, error);
            if (error)
                return refused(problem, "cannot make " + parent.string() + ": " + error.message());
        }
        const std::string temporary = path + ".vkdaedalus.tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return refused(problem, "cannot write " + temporary);
            file << text;
            if (!file.flush())
            {
                file.close();
                std::remove(temporary.c_str());
                return refused(problem, "writing " + temporary + " failed");
            }
        }
        if (status.type() == fs::file_type::regular)
        {
            fs::permissions(temporary, fs::status(path, error).permissions(), error);
            if (error)
            {
                std::remove(temporary.c_str());
                return refused(problem, "cannot keep the permissions of " + path + ": " + error.message());
            }
        }
        if (std::rename(temporary.c_str(), path.c_str()) != 0)
        {
            std::remove(temporary.c_str());
            return refused(problem, "cannot replace " + path);
        }
        return true;
    }

    bool saveLsfgFile(const std::string& path, const std::optional<FrameGenerationGlobal>& global,
                      const std::optional<FrameGenerationProfile>& profile, LsfgFile& written, std::string& problem)
    {
        const LsfgRead read = readLsfgFile(path);
        if (!read.file)
            return refused(problem, read.exists ? "lsfg-vk refuses " + path + ": " + read.problem : read.problem);
        const std::optional<std::string> text = lsfgTextWith(read.text, global, profile, problem);
        if (!text)
            return false;
        if (read.exists && *text == read.text)
        {
            written = *read.file;
            return true;
        }
        if (!writeLsfgText(path, *text, problem))
            return false;
        std::string unused;
        written = *parseLsfgFile(*text, unused);
        return true;
    }

    std::optional<size_t> lsfgProfileFor(const LsfgFile& file, const LsfgProcess& process)
    {
        const auto first = [&](const auto& matches) -> std::optional<size_t> {
            for (size_t i = 0; i < file.profiles.size(); i++)
                for (const std::string& active : file.profiles[i].activeIn)
                    if (matches(active))
                        return i;
            return std::nullopt;
        };
        if (process.profileNamed)
            for (size_t i = 0; i < file.profiles.size(); i++)
                if (file.profiles[i].name == *process.profileNamed)
                    return i;
        if (const auto byExecutable = first([&](const std::string& active) { return endsWith(process.executable, active); }))
            return byExecutable;
        for (const std::string& exe : process.mappedExes)
            if (const auto byWineExecutable = first([&](const std::string& active) { return endsWith(exe, active); }))
                return byWineExecutable;
        if (const auto byName = first([&](const std::string& active) { return process.processName == active; }))
            return byName;
        if (process.steamAppId)
            return first([&](const std::string& active) { return *process.steamAppId == active; });
        return std::nullopt;
    }

    LsfgProcess thisLsfgProcess()
    {
        LsfgProcess     process;
        std::error_code unreadable;
        process.executable = fs::read_symlink("/proc/self/exe", unreadable).string();
        std::ifstream maps("/proc/self/maps");
        std::string   line;
        while (std::getline(maps, line))
        {
            if (!endsWith(line, ".exe"))
                continue;
            size_t begin = line.find_first_of('/');
            if (begin == std::string::npos)
                begin = line.find_first_of(' ');
            if (begin != std::string::npos)
                process.mappedExes.push_back(line.substr(begin));
        }
        std::ifstream comm("/proc/self/comm");
        std::getline(comm, process.processName);
        process.profileNamed = environment("LSFGVK_PROFILE");
        process.steamAppId   = environment("SteamAppId");
        return process;
    }

    LsfgProcess lsfgProcessOf(const std::vector<std::string>& command, const std::string& program)
    {
        LsfgProcess process;
        const bool  windows = program.size() >= 4 && lowered(program.substr(program.size() - 4)) == ".exe";
        for (const std::string& word : command)
        {
            const bool named = endsWith(word, program) && (word.size() == program.size() || word[word.size() - program.size() - 1] == '/' ||
                                                           word[word.size() - program.size() - 1] == '\\');
            if (named && windows)
                process.mappedExes.push_back(word);
            else if (named)
                process.executable = word;
        }
        process.processName  = program.substr(0, 15);
        process.profileNamed = environment("LSFGVK_PROFILE");
        process.steamAppId   = environment("SteamAppId");
        return process;
    }

    bool lsfgSettingsFromEnvironment()
    {
        return std::getenv("LSFGVK_ENV") != nullptr;
    }

    std::string lsfgProfileNameOf(const std::string& profile)
    {
        return ownProfilePrefix + profile;
    }

    bool madeByVkDaedalus(const std::string& lsfgProfileName)
    {
        return lsfgProfileName.size() > ownProfilePrefix.size() && lsfgProfileName.compare(0, ownProfilePrefix.size(), ownProfilePrefix) == 0;
    }

    std::vector<std::string> lsfgActiveIn(const std::vector<std::string>& games)
    {
        std::vector<std::string> entries;
        for (const std::string& game : games)
            if (const std::string entry = activeIn(game); std::find(entries.begin(), entries.end(), entry) == entries.end())
                entries.push_back(entry);
        return entries;
    }

    FrameGenerationProfile newLsfgProfileFor(const std::string& profileName, const std::vector<std::string>& games, uint32_t multiplier, const LsfgColour& colour)
    {
        FrameGenerationProfile profile;
        profile.name             = lsfgProfileNameOf(profileName);
        profile.activeIn         = lsfgActiveIn(games);
        profile.multiplier       = multiplier;
        profile.colorSpace       = colour.colorSpace;
        profile.transferFunction = colour.transferFunction;
        return profile;
    }

    std::vector<FrameGenerationProfile> lsfgProfilesToSync(const LsfgFile& file, const std::map<std::string, std::vector<std::string>>& gamesByProfile)
    {
        LsfgFile ownersOwn;
        for (const FrameGenerationProfile& profile : file.profiles)
            if (!madeByVkDaedalus(profile.name))
                ownersOwn.profiles.push_back(profile);
        const auto ownedElsewhere = [&](const std::string& game) {
            LsfgProcess process;
            if (game.size() >= 4 && lowered(game.substr(game.size() - 4)) == ".exe")
                process.mappedExes.push_back("/" + game);
            else
                process.executable = "/" + game;
            process.processName = game.substr(0, 15);
            return lsfgProfileFor(ownersOwn, process).has_value();
        };
        std::set<std::string> known;
        for (const auto& [_, games] : gamesByProfile)
            for (const std::string& entry : lsfgActiveIn(games))
                known.insert(entry);
        std::vector<FrameGenerationProfile> changed;
        for (const FrameGenerationProfile& profile : file.profiles)
        {
            if (!madeByVkDaedalus(profile.name))
                continue;
            const auto               users = gamesByProfile.find(profile.name.substr(ownProfilePrefix.size()));
            std::vector<std::string> games;
            if (users != gamesByProfile.end())
                std::copy_if(users->second.begin(), users->second.end(), std::back_inserter(games), [&](const std::string& game) { return !ownedElsewhere(game); });
            const std::vector<std::string> assigned = lsfgActiveIn(games);
            std::vector<std::string> wanted;
            for (const std::string& entry : profile.activeIn)
                if (!known.count(entry) || std::find(assigned.begin(), assigned.end(), entry) != assigned.end())
                    wanted.push_back(entry);
            for (const std::string& entry : assigned)
                if (std::find(wanted.begin(), wanted.end(), entry) == wanted.end())
                    wanted.push_back(entry);
            if (wanted != profile.activeIn)
            {
                FrameGenerationProfile next = profile;
                next.activeIn               = wanted;
                changed.push_back(next);
            }
        }
        return changed;
    }

    std::optional<std::string> lsfgTextRenamed(const std::string& text, const std::string& from, const std::string& to, std::string& problem)
    {
        std::string out = text;
        if (!renameProfile(out, from, to, problem))
            return std::nullopt;
        std::string                   refusal;
        const std::optional<LsfgFile> back = parseLsfgFile(out, refusal);
        if (!back)
        {
            problem = "lsfg-vk would refuse its file after this change: " + refusal;
            return std::nullopt;
        }
        if (out != text && (profileNamed(*back, from) || !profileNamed(*back, to)))
        {
            problem = "the rename did not read back from lsfg-vk's file as it was written";
            return std::nullopt;
        }
        return out;
    }

    bool syncLsfgActiveIn(const std::string& path, const std::map<std::string, std::vector<std::string>>& gamesByProfile, std::string& problem)
    {
        const LsfgRead read = readLsfgFile(path);
        if (!read.exists)
            return read.problem.empty() || refused(problem, read.problem);
        if (!read.file)
            return refused(problem, "lsfg-vk refuses " + path + ": " + read.problem);
        std::string text = read.text;
        for (const FrameGenerationProfile& profile : lsfgProfilesToSync(*read.file, gamesByProfile))
        {
            const std::optional<std::string> next = lsfgTextWith(text, std::nullopt, profile, problem);
            if (!next)
                return false;
            text = *next;
        }
        return text == read.text || writeLsfgText(path, text, problem);
    }

    bool renameLsfgProfile(const std::string& path, const std::string& from, const std::string& to, std::string& problem)
    {
        const LsfgRead read = readLsfgFile(path);
        if (!read.exists)
            return read.problem.empty() || refused(problem, read.problem);
        if (!read.file)
            return refused(problem, "lsfg-vk refuses " + path + ": " + read.problem);
        const std::optional<std::string> text = lsfgTextRenamed(read.text, from, to, problem);
        if (!text)
            return false;
        return *text == read.text || writeLsfgText(path, *text, problem);
    }

    LsfgColour lsfgColourOf(VkColorSpaceKHR colorSpace)
    {
        switch (colorSpace)
        {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return {"srgb", "srgb"};
            case VK_COLOR_SPACE_HDR10_ST2084_EXT: return {"hdr10", "pq"};
            case VK_COLOR_SPACE_HDR10_HLG_EXT: return {"hdr10", "hlg"};
            case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return {"hdr10", "linear"};
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return {"scrgb", "linear"};
            case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return {"scrgb", "scrgbnl"};
            case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT: return {"dcip3", "dcip3"};
            default: return {};
        }
    }

    bool generatesFrames(const FrameGenerationProfile* profile)
    {
        return profile != nullptr && profile->multiplier >= lowestOnMultiplier;
    }

    std::optional<std::string> lsfgDllPath(const FrameGenerationGlobal& global)
    {
        if (const std::optional<std::string> named = environment("LSFGVK_DLL_PATH"))
            return fixDllPath(*named).string();
        if (global.dll)
            return fixDllPath(expandHome(*global.dll)).string();
        const std::array<const char*, 5> steamFolders = {".local/share/Steam/steamapps/common", ".steam/steam/steamapps/common",
                                                         ".steam/debian-installation/steamapps/common",
                                                         ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common",
                                                         "snap/steam/common/.local/share/Steam/steamapps/common"};
        for (const char* base : {"XDG_DATA_HOME", "HOME"})
            if (const std::optional<std::string> prefix = environment(base))
                for (const char* folder : steamFolders)
                    if (const fs::path path = fs::path(*prefix) / folder / "Lossless Scaling" / "lsfg-vk.dll"; isFile(path))
                        return path.string();
        std::error_code noDirectory;
        const fs::path  here = fs::current_path(noDirectory) / "lsfg-vk.dll";
        if (!noDirectory && isFile(here))
            return here.string();
        return std::nullopt;
    }

    LsfgDllStatus lsfgDllStatus(const FrameGenerationGlobal& global)
    {
        LsfgDllStatus status;
        status.path  = lsfgDllPath(global);
        status.found = status.path && isFile(*status.path);
        if (!status.found)
            status.inSteamLibrary = lsfgDllInSteamLibraries();
        return status;
    }

    std::vector<std::string> steamLibraryFolders(const std::string& vdf)
    {
        struct Token
        {
            bool        quoted = false;
            std::string text;
        };
        std::vector<Token> tokens;
        for (size_t i = 0; i < vdf.size(); i++)
        {
            const char c = vdf[i];
            if (std::isspace(static_cast<unsigned char>(c)))
                continue;
            if (c == '/' && i + 1 < vdf.size() && vdf[i + 1] == '/')
                i = std::min(vdf.find('\n', i), vdf.size());
            else if (c == '{' || c == '}')
                tokens.push_back({false, std::string(1, c)});
            else if (c == '"')
            {
                Token token{true, ""};
                for (i++; i < vdf.size() && vdf[i] != '"'; i++)
                {
                    if (vdf[i] == '\\' && i + 1 < vdf.size())
                    {
                        const char escaped = vdf[++i];
                        token.text += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                    }
                    else
                        token.text += vdf[i];
                }
                tokens.push_back(token);
            }
            else
            {
                Token token{false, ""};
                for (; i < vdf.size() && !std::isspace(static_cast<unsigned char>(vdf[i])) && vdf[i] != '{' && vdf[i] != '}' && vdf[i] != '"'; i++)
                    token.text += vdf[i];
                i--;
                tokens.push_back(token);
            }
        }
        std::vector<std::string> folders;
        for (size_t i = 0; i + 1 < tokens.size(); i++)
            if (lowered(tokens[i].text) == "path" && tokens[i + 1].quoted && !tokens[i + 1].text.empty())
                folders.push_back(tokens[i + 1].text);
        return folders;
    }

    std::optional<std::string> lsfgDllInSteamLibraries()
    {
        const std::optional<std::string> home = environment("HOME");
        if (!home)
            return std::nullopt;
        const fs::path        dataHome = environment("XDG_DATA_HOME").value_or(*home + "/.local/share");
        const std::vector<fs::path> roots = {dataHome / "Steam",
                                             fs::path(*home) / ".local/share/Steam",
                                             fs::path(*home) / ".steam/steam",
                                             fs::path(*home) / ".steam/root",
                                             fs::path(*home) / ".steam/debian-installation",
                                             fs::path(*home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                                             fs::path(*home) / ".var/app/com.valvesoftware.Steam/data/Steam",
                                             fs::path(*home) / "snap/steam/common/.local/share/Steam"};
        for (const fs::path& root : roots)
            for (const char* list : {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"})
                if (isFile(root / list))
                    for (const std::string& library : steamLibraryFolders(readWhole(root / list)))
                        if (const fs::path dll = fs::path(library) / "steamapps/common/Lossless Scaling/lsfg-vk.dll"; isFile(dll))
                            return dll.string();
        return std::nullopt;
    }

    bool frameGenerationLayerLoaded()
    {
        static const bool loaded = [] {
            std::ifstream maps("/proc/self/maps");
            std::string   line;
            while (std::getline(maps, line))
                if (line.find("liblsfg-vk-layer") != std::string::npos)
                    return true;
            return false;
        }();
        return loaded;
    }

    std::optional<bool> lsfgLayerListedBy(PFN_vkEnumerateInstanceLayerProperties enumerate)
    {
        uint32_t count = 0;
        if (enumerate == nullptr || enumerate(&count, nullptr) != VK_SUCCESS)
            return std::nullopt;
        std::vector<VkLayerProperties> layers(count);
        const VkResult                 listed = enumerate(&count, layers.data());
        if (listed != VK_SUCCESS && listed != VK_INCOMPLETE)
            return std::nullopt;
        layers.resize(count);
        return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) { return lsfgLayerName == layer.layerName; });
    }

    std::optional<bool> frameGenerationLayerVisible()
    {
        static const std::optional<bool> visible = []() -> std::optional<bool> {
            void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
            if (loader == nullptr)
                return std::nullopt;
            const std::optional<bool> listed =
                lsfgLayerListedBy(reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(dlsym(loader, "vkEnumerateInstanceLayerProperties")));
            dlclose(loader);
            return listed;
        }();
        return visible;
    }
}
