#include "field_editor.hpp"
#include "imgui.h"
#include "overlay/frame_draw.hpp"
#include "overlay/widgets.hpp"

namespace vkBasalt
{
    namespace
    {
        std::string defaultShown(const EffectParam& param)
        {
            const std::unique_ptr<EffectParam> reset = param.clone();
            reset->resetToDefault();
            if (const auto* choice = dynamic_cast<const IntParam*>(reset.get()); choice && choice->value >= 0 && static_cast<size_t>(choice->value) < choice->items.size())
                return choice->items[static_cast<size_t>(choice->value)];
            std::string shown;
            for (const auto& [_, value] : reset->serialize())
                shown += (shown.empty() ? "" : ", ") + value;
            return shown;
        }
    }

    FieldEditorFactory& FieldEditorFactory::instance()
    {
        static FieldEditorFactory factory;
        return factory;
    }

    void FieldEditorFactory::registerEditor(ParamType type, CreatorFunc creator)
    {
        creators[type] = creator;
    }

    FieldEditor* FieldEditorFactory::getEditor(ParamType type)
    {
        auto it = editors.find(type);
        if (it != editors.end())
            return it->second.get();

        auto creatorIt = creators.find(type);
        if (creatorIt == creators.end())
            return nullptr;

        editors[type] = creatorIt->second();
        return editors[type].get();
    }

    bool renderFieldEditor(EffectParam& param)
    {
        FieldEditor* editor = FieldEditorFactory::instance().getEditor(param.getType());
        if (!editor)
            return false;

        ui::panelRow();
        ImGui::BeginGroup();
        bool changed = editor->render(param);
        ImGui::EndGroup();

        const bool cut = !ui::parameterNameFits(param.label.c_str());
        if ((cut || !param.tooltip.empty()) && ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            if (cut)
                ImGui::TextUnformatted(param.label.c_str());
            if (!param.tooltip.empty())
                ImGui::TextUnformatted(param.tooltip.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        const bool moved = param.hasChanged();
        if (defaultMark(param.label.c_str(), moved, moved ? defaultShown(param) : "", activePalette()))
        {
            editor->resetToDefault(param);
            changed = true;
        }
        return changed;
    }

}
