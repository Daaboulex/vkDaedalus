#include "overlay/overlay_style.hpp"
#include "overlay/widgets.hpp"
#include "overlay/palette_names.hpp"

#include <algorithm>

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static bool opaque(ImGuiCol colour)
{
    return ((ImGui::GetColorU32(colour) >> IM_COL32_A_SHIFT) & 0xFF) == 0xFF;
}

int main()
{
    ImGuiContext* context = ImGui::CreateContext();
    const auto luminance = [](const ImVec4& c) {
        const auto channel = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * channel(c.x) + 0.7152f * channel(c.y) + 0.0722f * channel(c.z);
    };
    const auto contrast = [&](const ImVec4& a, const ImVec4& b) {
        const float la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    };
    for (const PaletteName& name : paletteNames)
    {
        const OverlayPalette& p = overlayPalette(name.word);
        for (const ImVec4* background : {&p.panel, &p.raised, &p.row, &p.control})
        {
            expect(contrast(p.text, *background) >= 4.5f && contrast(p.muted, *background) >= 4.5f, "body and muted text read at 4.5:1 on every surface of every palette");
            expect(contrast(p.warn, *background) >= 4.5f && contrast(p.ok, *background) >= 4.5f && contrast(p.bad, *background) >= 4.5f,
                   "warnings, success and errors read at 4.5:1 on every surface of every palette");
            expect(contrast(p.accent, *background) >= 3.0f, "the accent stands at 3:1 on every surface of every palette");
        }
        expect(contrast(p.onStrong, p.strong) >= 4.5f, "button text reads at 4.5:1 on every palette's buttons");
    }
    expect(&overlayPalette("no such scheme") == &overlayPalette("soft"), "an unknown scheme draws the soft Daaboulex one");
    useOverlayPalette("steel");
    expect(&activePalette() == &overlayPalette("steel"), "the chosen scheme is the one the views draw with");
    useOverlayPalette("soft");
    applyOverlayStyle(ImGui::GetStyle(), activePalette());
    expect(opaque(ImGuiCol_PopupBg), "menus and popups are drawn fully opaque, so nothing behind them shows through");
    expect(opaque(ImGuiCol_WindowBg), "cards are opaque; the game shows through the backdrop around them, never through a card");
    expect(opaque(ImGuiCol_Button) && opaque(ImGuiCol_FrameBg), "buttons and fields are opaque");

    ImGuiStyle stock;
    ImGui::StyleColorsDark(&stock);
    for (int slot = 0; slot < ImGuiCol_COUNT; slot++)
    {
        const ImVec4 mine = ImGui::GetStyle().Colors[slot];
        const ImVec4 dark = stock.Colors[slot];
        const bool   same = mine.x == dark.x && mine.y == dark.y && mine.z == dark.z && mine.w == dark.w;
        if (same && mine.w > 0.0f)
        {
            std::printf("FAIL: %s keeps ImGui's own dark colour, so it draws outside the palette\n", ImGui::GetStyleColorName(slot));
            failures++;
        }
    }

    const ImGuiStyle base = ImGui::GetStyle();
    for (float scale : {0.62f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f})
    {
        ImGuiStyle scaled;
        scaleOverlayStyle(scaled, base, scale);
        const float frameHeight = FontSize::body * scale + scaled.FramePadding.y * 2.0f;
        if (std::fabs(frameHeight - ui::Size::control * scale) > 0.01f)
        {
            std::printf("FAIL: at scale %.2f ImGui's own frame height is %.2f, not the kit's %.2f, so a card sized on one overflows the other\n", scale, frameHeight,
                        ui::Size::control * scale);
            failures++;
        }
    }

    const OverlayFonts fonts = loadOverlayFonts(*ImGui::GetIO().Fonts);
    for (ImFont* font : {fonts.body, fonts.bold, fonts.heading})
    {
        expect(font != nullptr, "each built-in font loads");
        if (font == nullptr)
            continue;
        ImFontBaked* baked = font->GetFontBaked(FontSize::body);
        expect(baked != nullptr && baked->FindGlyphNoFallback('W') != nullptr && baked->FindGlyphNoFallback('7') != nullptr,
               "each built-in font has the letters and digits the overlay draws");
    }
    expect(fonts.body != fonts.bold && fonts.bold != fonts.heading, "the body, bold and heading faces are three fonts");

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    const auto footerUnder = [](float contentHeight) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::Dummy(ImVec2(10.0f, contentHeight));
        const float contentBottom = ImGui::GetItemRectMax().y;
        ui::footer();
        const float footerTop = ImGui::GetItemRectMin().y;
        ImGui::End();
        ImGui::EndFrame();
        return std::pair(contentBottom, footerTop);
    };
    const auto [shortBottom, shortFooter] = footerUnder(20.0f);
    expect(shortFooter > shortBottom + 150.0f, "under short content the footer stays pinned to the card's bottom");
    const auto [tallBottom, tallFooter] = footerUnder(500.0f);
    expect(tallFooter >= tallBottom, "under content taller than the card the footer goes below it, never over it");
    float tableFooter = 0.0f;
    for (int frame = 0; frame < 2; frame++)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12.0f, 2.0f));
        const bool table = ImGui::BeginTable("columns", 2);
        ImGui::PopStyleVar();
        if (table)
        {
            ImGui::TableNextColumn();
            ImGui::BeginChild("filled", ImVec2(0.0f, ui::heightLeftAboveFooter()));
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ui::footer();
        tableFooter = ImGui::GetItemRectMin().y;
        ImGui::End();
        ImGui::EndFrame();
    }
    expect(tableFooter == shortFooter, "a table cell filled to the height left above the footer leaves the footer where it is pinned");

    ImGui::NewFrame();
    ui::beginFrame(fonts);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("panels", nullptr, ImGuiWindowFlags_NoDecoration);
    const float widthBefore = ImGui::GetContentRegionAvail().x;
    ui::beginPanel();
    ui::factRow("first", "1 ms");
    const ImVec2 firstMin = ImGui::GetItemRectMin();
    const ImVec2 firstMax = ImGui::GetItemRectMax();
    ui::factRow("second", "2 ms", ui::Mark::Estimated);
    const ImVec2 secondMin = ImGui::GetItemRectMin();
    const ImVec2 secondMax = ImGui::GetItemRectMax();
    const float  insideWidth = ImGui::GetContentRegionAvail().x;
    ui::panelRow(ui::Size::control);
    const float between = ui::px(ui::Size::groupGap);
    const float half    = (ImGui::GetContentRegionAvail().x - between) * 0.5f;
    ui::factCell("left", "5 ms", ui::Mark::Measured, nullptr, half);
    const ImVec2 leftMin = ImGui::GetItemRectMin();
    ImGui::SameLine(0.0f, between);
    ui::factCell("right", "6 ms", ui::Mark::Measured, nullptr, half);
    const ImVec2 rightMin = ImGui::GetItemRectMin();
    const ImVec2 rightMax = ImGui::GetItemRectMax();
    ui::endPanel();
    expect(secondMin.y == firstMax.y, "a panel's rows sit one under another with no gap");
    expect(leftMin.y == secondMax.y && rightMin.y == leftMin.y, "two cells in one panel row share that row, under the row before");
    expect(rightMin.x > leftMin.x + half && rightMax.x > secondMax.x - 0.5f && rightMax.x < secondMax.x + 0.5f, "a row of two cells ends where a full row ends");
    expect(firstMin.x > 0.0f && insideWidth < widthBefore, "a panel's rows are inset from its edges");
    expect(ImGui::GetContentRegionAvail().x == widthBefore, "the content width comes back after a panel");
    if (ImGui::BeginTable("two", 2))
    {
        ImGui::TableNextColumn();
        ui::beginPanel();
        ui::factRow("in a table", "3 ms");
        ui::endPanel();
        ImGui::TableNextColumn();
        ui::beginPanel();
        ui::beginPanel();
        ui::factRow("nested", "4 ms");
        ui::endPanel();
        ui::endPanel();
        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::Render();
    expect(ImGui::GetDrawData() != nullptr && ImGui::GetDrawData()->CmdLists.Size > 0, "panels inside a table and inside each other still draw");
    ImGui::DestroyContext(context);
    if (failures)
        std::printf("%d overlay style check(s) failed\n", failures);
    return failures ? 1 : 0;
}
