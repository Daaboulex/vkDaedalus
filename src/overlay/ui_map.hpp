#pragma once

struct ImGuiContext;

namespace vkBasalt
{
    namespace UiMap
    {
        void enableIfAsked(ImGuiContext* context);
        void beginFrame();
        void endFrame();
        void nameNextItem(const char* label);
        void markChecked(unsigned int id, bool checked);
    }
}
