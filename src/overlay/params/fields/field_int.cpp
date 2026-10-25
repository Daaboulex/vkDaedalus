#include "../field_editor.hpp"
#include "imgui.h"
#include "overlay/widgets.hpp"

namespace vkBasalt
{
    class IntFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<IntParam&>(param);
            bool changed = false;

            if (!p.items.empty())
            {
                if (p.uiType == "radio" ? ui::parameterSegments(p.label.c_str(), &p.value, p.items) : ui::parameterChoice(p.label.c_str(), &p.value, p.items))
                    changed = true;
            }
            else
            {
                if (ui::parameterSlider(p.label.c_str(), ImGuiDataType_S32, &p.value, 1, &p.minValue, &p.maxValue, formatWithUnits("%d", p.units).c_str()))
                {
                    if (p.step > 0.0f)
                    {
                        int step = static_cast<int>(p.step);
                        if (step > 0)
                            p.value = (p.value / step) * step;
                    }
                    changed = true;
                }
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::Int, IntFieldEditor)

}
