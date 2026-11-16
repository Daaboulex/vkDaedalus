#include "toml_edit.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace vkBasalt
{
    namespace fs = std::filesystem;

    Positions::Positions(const std::string& text) : text(text), lineStarts{0}
    {
        for (size_t i = 0; i < text.size(); i++)
            if (text[i] == '\n')
                lineStarts.push_back(i + 1);
    }

    size_t Positions::at(const toml::source_position& position) const
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

    size_t Positions::lineStart(size_t offset) const
    {
        const size_t newline = offset == 0 ? std::string::npos : text.rfind('\n', offset - 1);
        return newline == std::string::npos ? 0 : newline + 1;
    }

    size_t Positions::afterLine(size_t offset) const
    {
        const size_t newline = text.find('\n', offset);
        return newline == std::string::npos ? text.size() : newline + 1;
    }

    void TableEditor::set(std::string_view key, const std::optional<std::string>& wanted, bool same)
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

    void TableEditor::finish()
    {
        if (appended.empty())
            return;
        toml::source_position last = table.source().end;
        for (const auto& [_, value] : table)
            if (value.source().end.line > last.line || (value.source().end.line == last.line && value.source().end.column > last.column))
                last = value.source().end;
        const size_t at           = positions.afterLine(positions.at(last));
        const bool   needsNewline = at == positions.text.size() && !positions.text.empty() && positions.text.back() != '\n';
        edits.push_back({at, at, (needsNewline ? "\n" : "") + appended});
    }

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

    std::optional<std::string> optionalString(const std::optional<std::string>& value, bool& clean)
    {
        if (!value)
            return std::nullopt;
        const std::optional<std::string> text = quoted(*value);
        clean                                 = clean && text.has_value();
        return text;
    }

    std::string floatText(float value)
    {
        char        text[32];
        const auto  written = std::to_chars(text, text + sizeof(text), value);
        std::string out(text, written.ptr);
        if (out.find_first_of(".e") == std::string::npos)
            out += ".0";
        return out;
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

    bool refused(std::string& problem, std::string why)
    {
        problem = std::move(why);
        return false;
    }

    bool replaceFileAtomically(const std::string& path, const std::string& text, std::string_view whoseFile, std::string& problem)
    {
        std::error_code       error;
        const fs::file_status status = fs::symlink_status(path, error);
        if (status.type() == fs::file_type::symlink)
            return refused(problem, path + " is a link, and the overlay writes " + std::string(whoseFile) + " only where it is a plain file");
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
}
