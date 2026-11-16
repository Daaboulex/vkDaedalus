#ifndef TOML_EDIT_HPP_INCLUDED
#define TOML_EDIT_HPP_INCLUDED

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

    bool                       opensWith(const Positions& positions, const toml::table& table, std::string_view header);
    std::string                applied(std::string text, std::vector<TextEdit> edits);
    bool                       validUtf8(const std::string& text);
    std::optional<std::string> quoted(const std::string& text);
    std::optional<std::string> optionalString(const std::optional<std::string>& value, bool& clean);
    std::string                floatText(float value);
    std::optional<toml::table> tomlOf(const std::string& text, std::string& problem);
    bool                       refused(std::string& problem, std::string why);
    bool replaceFileAtomically(const std::string& path, const std::string& text, std::string_view whoseFile, std::string& problem);
}

#endif
