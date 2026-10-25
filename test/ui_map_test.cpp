#include "overlay/ui_map.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "imgui.h"

static std::string lineFor(const std::string& map, const std::string& label)
{
    std::istringstream lines(map);
    std::string        line;
    while (std::getline(lines, line))
    {
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (second != std::string::npos && line.substr(first + 1, second - first - 1) == label)
            return line;
    }
    return "";
}

static std::string lineIn(const std::string& map, const std::string& window, const std::string& label)
{
    std::istringstream lines(map);
    std::string        line;
    while (std::getline(lines, line))
        if (line.rfind(window + "\t" + label + "\t", 0) == 0)
            return line;
    return "";
}

static std::string lastField(const std::string& line, int fromEnd)
{
    size_t end = line.size();
    for (int i = 0; i < fromEnd; i++)
        end = line.rfind('\t', end - 1);
    const size_t start = line.rfind('\t', end - 1);
    return line.substr(start + 1, end - start - 1);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: ui-map-test <map file>\n");
        return 2;
    }
    setenv("VKDAEDALUS_UI_MAP", argv[1], 1);
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    vkBasalt::UiMap::enableIfAsked(ImGui::GetCurrentContext());

    bool checked = true;
    for (int frame = 0; frame < 2; frame++)
    {
        io.MousePos     = ImVec2(37.0f, 51.0f);
        io.MouseDown[1] = frame == 1;
        ImGui::NewFrame();
        vkBasalt::UiMap::beginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        static bool open = true;
        ImGui::Begin("vkDaedalus", &open);
        ImGui::Checkbox("Safe Anti-Cheat", &checked);
        ImGui::BeginDisabled(true);
        ImGui::Selectable("unsafe");
        ImGui::EndDisabled();
        ImGui::Selectable("safe");
        if (ImGui::BeginTabBar("Tabs"))
        {
            if (ImGui::BeginTabItem("First"))
                ImGui::EndTabItem();
            if (ImGui::BeginTabItem("Second"))
                ImGui::EndTabItem();
            ImGui::EndTabBar();
        }
        vkBasalt::UiMap::nameNextItem("##choice");
        if (ImGui::BeginCombo("##choice", "a"))
            ImGui::EndCombo();
        static float pair[2] = {0.0f, 0.0f};
        vkBasalt::UiMap::nameNextItem("P Float2");
        ImGui::SliderFloat2("P Float2", pair, 0.0f, 1.0f);
        ImGui::Button("Done");
        ImGui::SameLine(600.0f);
        ImGui::Button("Hidden");
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(420, 0));
        ImGui::SetNextWindowSize(ImVec2(300, 100));
        ImGui::Begin("vkDaedalus card", nullptr, ImGuiWindowFlags_NoDecoration);
        vkBasalt::UiMap::nameNextItem("Tile");
        ImGui::InvisibleButton("Tile", ImVec2(100, 40));
        vkBasalt::UiMap::markChecked(ImGui::GetItemID(), true);
        ImGui::BeginChild("##body", ImVec2(200, 30));
        ImGui::Dummy(ImVec2(10, 90));
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(10, 400));
        ImGui::End();
        ImGui::Render();
        vkBasalt::UiMap::endFrame();
    }

    std::ifstream      file(argv[1]);
    std::ostringstream content;
    content << file.rdbuf();
    const std::string map = content.str();
    const std::string box = lineFor(map, "Safe Anti-Cheat");
    const std::string unsafe = lineFor(map, "unsafe");
    const std::string safe = lineFor(map, "safe");
    int failures = 0;
    if (map.rfind("frame 2 mouse 37 51 0 1 0\n", 0) != 0)
    {
        std::printf("FAIL: the map does not start with its frame number and the mouse the overlay saw in it (position, then left, right and middle down): %s\n",
                    map.substr(0, map.find('\n')).c_str());
        failures++;
    }
    if (box.empty() || box.rfind("vkDaedalus\t", 0) != 0 || lastField(box, 1) != "1")
    {
        std::printf("FAIL: the checked checkbox is missing, in the wrong window or not marked checked: %s\n", box.c_str());
        failures++;
    }
    if (unsafe.empty() || lastField(unsafe, 2) != "1")
    {
        std::printf("FAIL: an item inside BeginDisabled is not marked disabled: %s\n", unsafe.c_str());
        failures++;
    }
    if (safe.empty() || lastField(safe, 2) != "0")
    {
        std::printf("FAIL: an enabled item is marked disabled or missing: %s\n", safe.c_str());
        failures++;
    }
    for (const char* label : {"First", "Second", "##choice", "P Float2", "Done"})
    {
        const std::string line = lineFor(map, label);
        if (line.empty() || std::stof(lastField(line, 3)) <= std::stof(lastField(line, 5)))
        {
            std::printf("FAIL: %s is missing from the map or has no box: %s\n", label, line.c_str());
            failures++;
        }
    }
    const std::string hidden = lineFor(map, "(an item past the window edge)");
    if (hidden.empty() || hidden.rfind("vkDaedalus\t", 0) != 0 || std::stof(lastField(hidden, 6)) < 400.0f)
    {
        std::printf("FAIL: a button ImGui clipped wholly past the window's right edge is missing from the map: %s\n", hidden.c_str());
        failures++;
    }
    const std::string bar = lineFor(map, "(title bar)");
    if (bar.empty() || bar.rfind("vkDaedalus\t", 0) != 0 || std::stof(lastField(bar, 4)) < 399.0f || std::stof(lastField(bar, 3)) <= std::stof(lastField(bar, 5)))
    {
        std::printf("FAIL: a window with a close button has no title bar in the map, so a test cannot find its X: %s\n", bar.c_str());
        failures++;
    }
    const std::string cardScroll = lineIn(map, "vkDaedalus card", "(scroll range)");
    if (cardScroll.empty() || std::stof(lastField(cardScroll, 3)) <= 0.0f)
    {
        std::printf("FAIL: a window without a title bar that overflows reports no scroll range, so a page that does not fit goes unseen: %s\n", cardScroll.c_str());
        failures++;
    }
    std::string body;
    {
        std::istringstream lines(map);
        std::string        line;
        while (std::getline(lines, line))
            if (line.rfind("vkDaedalus card/##body_", 0) == 0 && line.find("\t(scroll range)\t") != std::string::npos)
                body = line;
    }
    if (body.empty() || std::stof(lastField(body, 3)) <= 0.0f)
    {
        std::printf("FAIL: a child window that overflows, as a card's body does, reports no scroll range, so a body below the fold goes unseen: %s\n", body.c_str());
        failures++;
    }
    const std::string tile = lineIn(map, "vkDaedalus card", "Tile");
    if (tile.empty() || lastField(tile, 1) != "1")
    {
        std::printf("FAIL: a tile marked checked is missing or not marked checked: %s\n", tile.c_str());
        failures++;
    }
    if (!lineFor(map, "Hidden").empty())
    {
        std::printf("FAIL: a wholly clipped button kept the label ImGui never reported for it\n");
        failures++;
    }
    ImGui::DestroyContext();
    if (failures != 0)
        return 1;
    std::printf("OK: the item map names each item's window, label, box, disabled and checked state, tabs, a named combo and a named vector slider included\n");
    return 0;
}
