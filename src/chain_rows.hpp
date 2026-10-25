#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vkBasalt
{
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
