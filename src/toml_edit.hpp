#ifndef TOML_EDIT_HPP_INCLUDED
#define TOML_EDIT_HPP_INCLUDED

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

namespace vkBasalt
{
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

        explicit Positions(const std::string& text);

        size_t at(const toml::source_position& position) const;
        size_t lineStart(size_t offset) const;
        size_t afterLine(size_t offset) const;
    };

    struct TableEditor
    {
        const Positions&       positions;
        const toml::table&     table;
        std::vector<TextEdit>& edits;
        std::string            appended;

        void set(std::string_view key, const std::optional<std::string>& wanted, bool same);
        void finish();
    };

    struct WriteRules
    {
        bool                                  followLinks    = false;
        bool                                  refuseReadOnly = false;
        std::optional<std::filesystem::perms> newFilePermissions;
    };

    struct FileText
    {
        bool        exists = false;
        std::string text;
        std::string problem;
    };

    bool        opensWith(const Positions& positions, const toml::table& table, std::string_view header);
    void        removeSection(const Positions& positions, const toml::table& table, std::vector<TextEdit>& edits);
    bool        renameProfileIn(std::string& text, const std::string& from, const std::string& to, std::string_view layer, std::string& problem);
    std::string applied(std::string text, std::vector<TextEdit> edits);
    bool        validUtf8(const std::string& text);
    std::optional<std::string> quoted(const std::string& text);
    std::optional<std::string> optionalString(const std::optional<std::string>& value, bool& clean);
    std::optional<std::string> activeInText(const std::vector<std::string>& games, bool& clean);
    std::string                floatText(float value);
    std::optional<toml::table> tomlOf(const std::string& text, std::string& problem);
    bool                       refused(std::string& problem, std::string why);
    FileText                   readFileText(const std::string& path);
    bool                       replaceFileAtomically(
                              const std::string& path, const std::string& text, std::string_view whoseFile, const WriteRules& rules, std::string& problem);
}

#endif
