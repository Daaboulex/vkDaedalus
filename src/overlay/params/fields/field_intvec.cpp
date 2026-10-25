#include "../field_editor.hpp"
#include "overlay/widgets.hpp"
#include "imgui.h"

namespace vkBasalt
{
    class IntVecFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<IntVecParam&>(param);
            bool changed = false;

            changed = ui::parameterSlider(p.label.c_str(), ImGuiDataType_S32, p.value, static_cast<int>(p.componentCount), &p.minValue[0], &p.maxValue[0], formatWithUnits("%d", p.units).c_str());

            if (changed && p.step > 0.0f)
            {
                int step = static_cast<int>(p.step);
                if (step > 0)
                {
                    for (uint32_t i = 0; i < p.componentCount; i++)
                        p.value[i] = (p.value[i] / step) * step;
                }
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::IntVec, IntVecFieldEditor)

}
