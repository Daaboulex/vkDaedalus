#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "technique_info.hpp"

namespace vkBasalt
{
    inline constexpr char techniqueSeparator = '/';

    inline std::string instanceOfRow(const std::string& row)
    {
        return row.substr(0, row.find(techniqueSeparator));
    }

    inline std::string techniqueOfRow(const std::string& row)
    {
        const size_t at = row.find(techniqueSeparator);
        return at == std::string::npos ? std::string() : row.substr(at + 1);
    }

    inline std::string rowNameOf(const std::string& instance, const std::string& technique, const std::vector<std::string>& techniques)
    {
        return techniques.empty() || technique.empty() || technique == techniques.front() ? instance
                                                                                         : instance + techniqueSeparator + technique;
    }

    template <typename TechniquesOf>
    std::vector<std::string> rowsWithEveryTechnique(const std::vector<std::string>& rows, const TechniquesOf& techniquesOf)
    {
        std::vector<std::string> out = rows;
        std::vector<std::string> instances;
        for (const std::string& row : rows)
            if (std::find(instances.begin(), instances.end(), instanceOfRow(row)) == instances.end())
                instances.push_back(instanceOfRow(row));
        for (const std::string& instance : instances)
        {
            const std::vector<std::string> techniques = techniquesOf(instance);
            size_t                         after      = 0;
            for (size_t i = 0; i < out.size(); i++)
                if (instanceOfRow(out[i]) == instance)
                    after = i + 1;
            for (const std::string& technique : techniques)
            {
                const std::string row = rowNameOf(instance, technique, techniques);
                if (std::find(out.begin(), out.end(), row) != out.end())
                    continue;
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(after), row);
                after++;
            }
        }
        return out;
    }

    inline const TechniqueInfo* techniqueOfRowIn(const std::string& row, const std::vector<TechniqueInfo>& techniques)
    {
        const std::string named = techniqueOfRow(row);
        for (const TechniqueInfo& technique : techniques)
            if (named.empty() ? &technique == &techniques.front() : technique.name == named)
                return &technique;
        return nullptr;
    }

    inline std::string rowLabelOf(const std::string& row, const std::vector<TechniqueInfo>& techniques)
    {
        const TechniqueInfo* technique = techniqueOfRowIn(row, techniques);
        if (techniques.size() <= 1 || technique == nullptr)
            return row;
        return instanceOfRow(row) + ": " + (technique->label.empty() ? technique->name : technique->label);
    }

    inline std::vector<bool> rowsDrawn(const std::vector<std::string>& rows, const std::vector<std::vector<TechniqueInfo>>& techniques)
    {
        if (techniques.size() != rows.size())
            throw std::invalid_argument("rowsDrawn needs the techniques of every row");
        std::vector<bool> drawn(rows.size());
        for (size_t i = 0; i < rows.size(); i++)
        {
            const TechniqueInfo* technique = techniqueOfRowIn(rows[i], techniques[i]);
            drawn[i]                       = technique == nullptr || !technique->hidden;
        }
        for (size_t i = 0; i < rows.size(); i++)
        {
            bool instanceDrawn = false;
            for (size_t j = 0; j < rows.size(); j++)
                instanceDrawn = instanceDrawn || (drawn[j] && instanceOfRow(rows[j]) == instanceOfRow(rows[i]));
            if (!instanceDrawn)
                drawn[i] = true;
        }
        return drawn;
    }

    inline std::optional<size_t> drawnNeighbour(const std::vector<bool>& drawn, size_t at, bool after)
    {
        if (after)
        {
            for (size_t j = at + 1; j < drawn.size(); j++)
                if (drawn[j])
                    return j;
            return std::nullopt;
        }
        for (size_t j = std::min(at, drawn.size()); j-- > 0;)
            if (drawn[j])
                return j;
        return std::nullopt;
    }

    template <typename EffectPtr>
    struct BuiltRows
    {
        std::vector<EffectPtr>   effects;
        std::vector<size_t>      rows;
        std::vector<std::string> dropped;
    };

    template <typename EffectPtr, typename SlotOf, typename Images, typename Build>
    BuiltRows<EffectPtr> buildRowsSkippingDropped(const std::vector<std::string>& names, size_t firstRow, size_t rows, const SlotOf& slotOf,
                                                  const Images& target, const Build& build)
    {
        BuiltRows<EffectPtr> built;
        std::vector<size_t>  readFrom;
        size_t               at = 0;
        for (size_t r = 0; r < rows; r++)
        {
            const bool last   = r + 1 == rows;
            EffectPtr  effect = build(firstRow + r, slotOf(at), last ? target : slotOf(at + 1));
            if (effect)
            {
                built.effects.push_back(std::move(effect));
                built.rows.push_back(firstRow + r);
                readFrom.push_back(at);
                at++;
                continue;
            }
            built.dropped.push_back(names[firstRow + r]);
            while (last && !built.effects.empty())
            {
                built.effects.pop_back();
                EffectPtr again = build(built.rows.back(), slotOf(readFrom.back()), target);
                if (again)
                {
                    built.effects.push_back(std::move(again));
                    break;
                }
                built.dropped.push_back(names[built.rows.back()]);
                built.rows.pop_back();
                readFrom.pop_back();
            }
        }
        return built;
    }
}
