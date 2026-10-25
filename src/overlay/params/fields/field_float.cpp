#include "../field_editor.hpp"
#include "imgui.h"
#include "overlay/widgets.hpp"
#include <cmath>

namespace vkBasalt
{
    class FloatFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<FloatParam&>(param);
            bool changed = false;

            if (ui::parameterSlider(p.label.c_str(), ImGuiDataType_Float, &p.value, 1, &p.minValue, &p.maxValue, formatWithUnits("%.3f", p.units).c_str()))
            {
                if (p.step > 0.0f)
                    p.value = std::round(p.value / p.step) * p.step;
                changed = true;
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::Float, FloatFieldEditor)

}
