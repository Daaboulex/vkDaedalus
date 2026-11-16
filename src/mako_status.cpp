#include "mako_status.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <tuple>

#include "prefix_holder.hpp"

namespace vkBasalt
{
    namespace fs = std::filesystem;

    namespace
    {
        enum class JsonKind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        struct Json
        {
            JsonKind                 kind = JsonKind::Null;
            bool                     truth = false;
            std::string              text;
            std::vector<std::string> keys;
            std::vector<Json>        items;

            const Json* member(std::string_view key) const
            {
                if (kind != JsonKind::Object)
                    return nullptr;
                for (size_t i = 0; i < keys.size(); i++)
                    if (keys[i] == key)
                        return &items[i];
                return nullptr;
            }
        };

        constexpr size_t deepestJsonNesting = 16;

        class JsonReader
        {
        public:
            explicit JsonReader(std::string_view text) : text(text) {}

            bool document(Json& out)
            {
                if (!value(out, 0))
                    return false;
                space();
                return at == text.size();
            }

        private:
            void space()
            {
                while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r'))
                    at++;
            }

            bool word(std::string_view expected)
            {
                if (text.substr(at, expected.size()) != expected)
                    return false;
                at += expected.size();
                return true;
            }

            bool string(std::string& out)
            {
                if (at >= text.size() || text[at] != '"')
                    return false;
                at++;
                while (at < text.size() && text[at] != '"')
                {
                    const char character = text[at++];
                    if (static_cast<unsigned char>(character) < 0x20)
                        return false;
                    if (character != '\\')
                    {
                        out += character;
                        continue;
                    }
                    if (at >= text.size())
                        return false;
                    const char escaped = text[at++];
                    switch (escaped)
                    {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u':
                        {
                            unsigned int code = 0;
                            if (at + 4 > text.size())
                                return false;
                            const auto [end, error] = std::from_chars(text.data() + at, text.data() + at + 4, code, 16);
                            if (error != std::errc() || end != text.data() + at + 4)
                                return false;
                            at += 4;
                            if (code < 0x80)
                                out += static_cast<char>(code);
                            else if (code < 0x800)
                            {
                                out += static_cast<char>(0xC0 | (code >> 6));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            else
                            {
                                out += static_cast<char>(0xE0 | (code >> 12));
                                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            break;
                        }
                        default: return false;
                    }
                }
                if (at >= text.size())
                    return false;
                at++;
                return true;
            }

            bool value(Json& out, size_t depth)
            {
                space();
                if (at >= text.size() || depth > deepestJsonNesting)
                    return false;
                const char first = text[at];
                if (first == '{' || first == '[')
                {
                    const bool object = first == '{';
                    const char close  = object ? '}' : ']';
                    out.kind          = object ? JsonKind::Object : JsonKind::Array;
                    at++;
                    space();
                    if (at < text.size() && text[at] == close)
                    {
                        at++;
                        return true;
                    }
                    while (true)
                    {
                        space();
                        if (object)
                        {
                            std::string key;
                            if (!string(key))
                                return false;
                            space();
                            if (at >= text.size() || text[at++] != ':')
                                return false;
                            out.keys.push_back(std::move(key));
                        }
                        Json item;
                        if (!value(item, depth + 1))
                            return false;
                        out.items.push_back(std::move(item));
                        space();
                        if (at >= text.size())
                            return false;
                        const char next = text[at++];
                        if (next == close)
                            return true;
                        if (next != ',')
                            return false;
                    }
                }
                if (first == '"')
                {
                    out.kind = JsonKind::String;
                    return string(out.text);
                }
                if (word("true") || word("false"))
                {
                    out.kind  = JsonKind::Bool;
                    out.truth = first == 't';
                    return true;
                }
                if (word("null"))
                    return true;
                const size_t start = at;
                while (at < text.size() && (std::string_view("+-.eE").find(text[at]) != std::string_view::npos || (text[at] >= '0' && text[at] <= '9')))
                    at++;
                out.kind = JsonKind::Number;
                out.text = std::string(text.substr(start, at - start));
                return at > start;
            }

            std::string_view text;
            size_t           at = 0;
        };

        const Json* found(const Json& root, std::initializer_list<std::string_view> path)
        {
            const Json* here = &root;
            for (const std::string_view key : path)
                if (here = here->member(key); here == nullptr)
                    return nullptr;
            return here;
        }

        std::string named(std::initializer_list<std::string_view> path)
        {
            std::string name;
            for (const std::string_view key : path)
                name += (name.empty() ? "" : ".") + std::string(key);
            return name;
        }

        template<typename Whole>
        bool whole(const Json& root, std::initializer_list<std::string_view> path, Whole& out, std::string& problem)
        {
            const Json* value = found(root, path);
            if (value != nullptr && value->kind == JsonKind::Number)
            {
                const auto [end, error] = std::from_chars(value->text.data(), value->text.data() + value->text.size(), out);
                if (error == std::errc() && end == value->text.data() + value->text.size())
                    return true;
            }
            problem = "MAKO's status file holds no whole number at " + named(path);
            return false;
        }

        bool words(const Json& root, std::initializer_list<std::string_view> path, std::string& out, std::string& problem)
        {
            const Json* value = found(root, path);
            if (value != nullptr && value->kind == JsonKind::String)
            {
                out = value->text;
                return true;
            }
            problem = "MAKO's status file holds no text at " + named(path);
            return false;
        }

        bool truth(const Json& root, std::initializer_list<std::string_view> path, bool& out, std::string& problem)
        {
            const Json* value = found(root, path);
            if (value != nullptr && value->kind == JsonKind::Bool)
            {
                out = value->truth;
                return true;
            }
            problem = "MAKO's status file holds no true or false at " + named(path);
            return false;
        }

        bool sameSize(uint32_t width, uint32_t height, uint32_t otherWidth, uint32_t otherHeight)
        {
            return width == otherWidth && height == otherHeight;
        }
    }

    std::optional<MakoStatus> parseMakoStatus(const std::string& text, std::string& problem)
    {
        Json root;
        if (!JsonReader(text).document(root) || root.kind != JsonKind::Object)
        {
            problem = "MAKO's status file is not a JSON object";
            return std::nullopt;
        }
        MakoStatus status;
        if (!words(root, {"role"}, status.role, problem) || !whole(root, {"context"}, status.context, problem)
            || !words(root, {"applied", "frame_generation_model"}, status.appliedModel, problem)
            || !truth(root, {"applied", "scaling_enabled"}, status.appliedScalingEnabled, problem)
            || !truth(root, {"spatial_scaling", "active"}, status.scalingActive, problem)
            || !words(root, {"spatial_scaling", "active_method"}, status.scalingMethod, problem)
            || !whole(root, {"spatial_scaling", "source_width"}, status.sourceWidth, problem)
            || !whole(root, {"spatial_scaling", "source_height"}, status.sourceHeight, problem)
            || !whole(root, {"spatial_scaling", "presentation_width"}, status.presentationWidth, problem)
            || !whole(root, {"spatial_scaling", "presentation_height"}, status.presentationHeight, problem))
            return std::nullopt;
        const Json* reason = found(root, {"spatial_scaling", "inactive_reason"});
        if (reason == nullptr || (reason->kind != JsonKind::Null && reason->kind != JsonKind::String))
        {
            problem = "MAKO's status file holds neither text nor null at spatial_scaling.inactive_reason";
            return std::nullopt;
        }
        if (reason->kind == JsonKind::String)
            status.scalingInactiveReason = reason->text;
        return status;
    }

    std::string makoStatusFolder(const std::string& configPath)
    {
        return (fs::path(configPath).parent_path() / "runtime-state").string();
    }

    bool isMakoStatusFileOf(std::string_view fileName, uint64_t pid, uint64_t startTicks)
    {
        const std::string process = std::to_string(pid) + "-" + std::to_string(startTicks) + "-";
        return fileName.starts_with(process) && fileName.ends_with(".json");
    }

    bool makoStatusIsOfSwapchain(const MakoStatus& status, uint32_t width, uint32_t height)
    {
        return sameSize(width, height, status.sourceWidth, status.sourceHeight) || sameSize(width, height, status.presentationWidth, status.presentationHeight);
    }

    MakoStatusRead readMakoStatus(const std::string& folder, uint64_t pid, uint64_t startTicks, uint32_t width, uint32_t height)
    {
        MakoStatusRead         read;
        std::error_code        error;
        fs::directory_iterator entries(folder, error);
        if (error == std::errc::no_such_file_or_directory)
            return read;
        if (error)
        {
            read.problem = "MAKO's status folder " + folder + " cannot be listed: " + error.message();
            return read;
        }
        for (const fs::directory_iterator end{}; entries != end; entries.increment(error))
        {
            if (error)
            {
                read.problem = "MAKO's status folder " + folder + " cannot be listed: " + error.message();
                return read;
            }
            const fs::path path = entries->path();
            if (!isMakoStatusFileOf(path.filename().string(), pid, startTicks))
                continue;
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                read.problem = "MAKO's status file " + path.string() + " cannot be opened";
                continue;
            }
            const std::string               text((std::istreambuf_iterator<char>(file)), {});
            std::string                     problem;
            const std::optional<MakoStatus> status = parseMakoStatus(text, problem);
            if (!status)
                read.problem = problem;
            else if (makoStatusIsOfSwapchain(*status, width, height))
                read.ofSwapchain.push_back(*status);
        }
        if (!read.ofSwapchain.empty())
            read.problem.clear();
        return read;
    }

    uint64_t thisProcessStartTicks()
    {
        std::ifstream     file("/proc/self/stat", std::ios::binary);
        const std::string stat((std::istreambuf_iterator<char>(file)), {});
        return processStartTicks(stat).value_or(0);
    }

    const MakoStatus* makoScalingStatus(const std::vector<MakoStatus>& ofSwapchain)
    {
        const auto rank = [](const MakoStatus& status) { return std::tuple(status.scalingActive, status.scalingInactiveReason.has_value(), status.context); };
        const auto best = std::max_element(ofSwapchain.begin(), ofSwapchain.end(), [&](const MakoStatus& a, const MakoStatus& b) { return rank(a) < rank(b); });
        return best == ofSwapchain.end() ? nullptr : &*best;
    }

    const MakoStatus* makoGenerationStatus(const std::vector<MakoStatus>& ofSwapchain)
    {
        const MakoStatus* newest = nullptr;
        for (const MakoStatus& status : ofSwapchain)
            if (status.role == makoGenerationRole && (newest == nullptr || status.context > newest->context))
                newest = &status;
        return newest;
    }

    bool makoFellBackToMakoFg(std::string_view modelAtStart, const std::vector<MakoStatus>& ofSwapchain)
    {
        const MakoStatus* generation = makoGenerationStatus(ofSwapchain);
        return generation != nullptr && modelAtStart == "lossless-scaling" && generation->appliedModel == "mako-fg";
    }

    std::string makoScalingSizes(const MakoStatus& status)
    {
        return std::to_string(status.sourceWidth) + "x" + std::to_string(status.sourceHeight) + " to " + std::to_string(status.presentationWidth) + "x"
               + std::to_string(status.presentationHeight);
    }

    std::string makoScalingInactiveWords(std::string_view reason)
    {
        if (reason == "factor-not-upscaling")
            return "MAKO does not scale: the factor is 1x, which scales nothing";
        if (reason == "application-extent-override-no-source-presentation-split")
            return "MAKO does not scale: the game asks for the window's full size, not the smaller size MAKO offers it";
        if (reason == "application-extent-mismatch")
            return "MAKO does not scale: the game asks for a size MAKO did not offer it";
        return "MAKO does not scale, and names the reason " + std::string(reason);
    }

    MakoScalingShown makoScalingShown(bool onInFile, bool makoRunsTheProfile, const MakoStatusRead& read)
    {
        if (!makoRunsTheProfile)
            return {onInFile ? MakoScalingRun::NotStarted : MakoScalingRun::Off, "", ""};
        const MakoStatus* status = makoScalingStatus(read.ofSwapchain);
        if (status != nullptr && status->scalingActive)
            return {MakoScalingRun::Scaling, makoScalingSizes(*status), status->scalingMethod};
        if (!onInFile)
            return {MakoScalingRun::Off, "", ""};
        if (status == nullptr)
            return {MakoScalingRun::NoStatus,
                    read.problem.empty() ? "MAKO has written no status file for this swapchain, so what it scales is not known" : read.problem, ""};
        if (status->scalingInactiveReason)
            return {MakoScalingRun::NotScaling, makoScalingInactiveWords(*status->scalingInactiveReason), ""};
        if (!status->appliedScalingEnabled)
            return {MakoScalingRun::NotStarted, "", ""};
        return {MakoScalingRun::NotScaling, "MAKO does not scale, and names no reason", ""};
    }
}
