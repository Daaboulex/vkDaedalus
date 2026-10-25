#include "../field_editor.hpp"
#include "imgui.h"
#include "overlay/widgets.hpp"

namespace vkBasalt
{
    class UintFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<UintParam&>(param);
            bool changed = false;

            if (ui::parameterSlider(p.label.c_str(), ImGuiDataType_U32, &p.value, 1, &p.minValue, &p.maxValue, formatWithUnits("%u", p.units).c_str()))
            {
                if (p.step > 0.0f)
                {
                    uint32_t step = static_cast<uint32_t>(p.step);
                    if (step > 0)
                        p.value = (p.value / step) * step;
                }
                changed = true;
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::Uint, UintFieldEditor)

}
