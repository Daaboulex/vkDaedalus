#include "../field_editor.hpp"
#include "overlay/widgets.hpp"
#include "imgui.h"
#include <cmath>

namespace vkBasalt
{
    class FloatVecFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<FloatVecParam&>(param);
            bool changed = false;

            if (p.uiType == "color" && p.componentCount >= 3)
                return ui::parameterColour(p.label.c_str(), p.value, static_cast<int>(p.componentCount));
            changed = ui::parameterSlider(p.label.c_str(), ImGuiDataType_Float, p.value, static_cast<int>(p.componentCount), &p.minValue[0], &p.maxValue[0],
                                          formatWithUnits("%.3f", p.units).c_str());

            if (changed && p.step > 0.0f)
            {
                for (uint32_t i = 0; i < p.componentCount; i++)
                    p.value[i] = std::round(p.value[i] / p.step) * p.step;
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::FloatVec, FloatVecFieldEditor)

}
