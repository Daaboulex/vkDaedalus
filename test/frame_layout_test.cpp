#include "overlay/frame_layout.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void expect(bool condition, const char* what, float width, float height, float scale)
{
    if (condition)
        return;
    std::printf("FAIL: %s (display %.0fx%.0f, desktop scale %.2f)\n", what, width, height, scale);
    failures++;
}

static bool inside(const vkBasalt::LayoutRect& r, const vkBasalt::LayoutRect& display)
{
    const float slack = 0.01f;
    return r.w >= 0.0f && r.h >= 0.0f && r.x >= display.x - slack && r.y >= display.y - slack && r.right() <= display.right() + slack
           && r.bottom() <= display.bottom() + slack;
}

static bool apart(const vkBasalt::LayoutRect& a, const vkBasalt::LayoutRect& b)
{
    const float slack = 0.01f;
    return a.right() <= b.x + slack || b.right() <= a.x + slack || a.bottom() <= b.y + slack || b.bottom() <= a.y + slack;
}

int main()
{
    using namespace vkBasalt;
    const std::vector<std::pair<float, float>> displays = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {800, 600},  {1024, 768}, {3440, 1440},
                                                           {1280, 1024}, {640, 360},   {1280, 800},  {1366, 768},  {5120, 1440}, {2560, 1600}, {1080, 1920},
                                                           {720, 1280},  {1600, 900},  {960, 720},   {1152, 864}};
    const std::vector<float> scales = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 0.5f, 0.0f, -1.0f, NAN};

    for (const auto& [w, h] : displays)
        for (float scale : scales)
        {
            const FrameLayout layout = frameLayout(w, h, scale);
            std::vector<LayoutRect> tiles;
            tiles.insert(tiles.end(), layout.leftTiles.begin(), layout.leftTiles.end());
            tiles.insert(tiles.end(), layout.sectionTiles.begin(), layout.sectionTiles.end());
            tiles.insert(tiles.end(), layout.rightTiles.begin(), layout.rightTiles.end());
            tiles.insert(tiles.end(), layout.iconTiles.begin(), layout.iconTiles.end());

            bool allInside = inside(layout.topBar, layout.display) && inside(layout.card(), layout.display);
            for (const LayoutRect& tile : tiles)
                allInside = allInside && inside(tile, layout.display);
            expect(allInside, "every part of the frame lies on the display", w, h, scale);

            bool noOverlap = true;
            for (size_t i = 0; i < tiles.size(); i++)
            {
                for (size_t j = i + 1; j < tiles.size(); j++)
                    noOverlap = noOverlap && apart(tiles[i], tiles[j]);
                noOverlap = noOverlap && apart(tiles[i], layout.topBar) && apart(tiles[i], layout.card());
            }
            expect(noOverlap, "no tile overlaps another tile, the top bar or the card", w, h, scale);

            float tilesLeft  = tiles.front().x;
            float tilesRight = tiles.front().right();
            for (const LayoutRect& tile : tiles)
            {
                tilesLeft  = std::min(tilesLeft, tile.x);
                tilesRight = std::max(tilesRight, tile.right());
            }
            expect(std::fabs(layout.card().x - tilesLeft) < 0.01f && std::fabs(layout.card().right() - tilesRight) < 0.01f,
                   "the card spans the tile row, edge to edge, on every page", w, h, scale);
            expect(layout.cardHeight > 0.0f, "a card has room to show", w, h, scale);
            expect(layout.scale >= fittedFrameScale(w, h, scale) - 0.0001f, "the layout chosen never reads smaller than the regular one", w, h, scale);
            if (layout.compact)
                expect(fittedCompactScale(w, h, scale) > fittedFrameScale(w, h, scale), "the compact layout is chosen only when it reads larger", w, h, scale);
            if (w >= 800.0f && h >= 600.0f && scale == 1.0f)
                expect(layout.scale >= 0.9f, "any display of at least 800 by 600 shows the overlay at 90% or more", w, h, scale);
        }

    const FrameLayout one  = frameLayout(3840, 2160, 1.0f);
    const FrameLayout more = frameLayout(3840, 2160, 1.5f);
    expect(std::fabs(more.sectionTiles[0].w / one.sectionTiles[0].w - 1.5f) < 0.001f && std::fabs(more.topBar.h / one.topBar.h - 1.5f) < 0.001f
               && std::fabs(more.card().w / one.card().w - 1.5f) < 0.001f,
           "on a display with room, the desktop scale scales the whole frame", 3840, 2160, 1.5f);

    const FrameLayout cramped = frameLayout(1280, 720, 2.0f);
    const FrameLayout fitting = frameLayout(1280, 720, 1.0f);
    expect(std::fabs(cramped.sectionTiles[0].w - fitting.sectionTiles[0].w) < 0.001f, "a scale too big for the display is reduced to what fits", 1280, 720, 2.0f);

    expect(!frameLayout(1280, 720, 1.0f).compact && !frameLayout(1920, 1080, 1.5f).compact && !frameLayout(3840, 2160, 2.0f).compact,
           "a 16:9 display with room keeps the regular layout", 1280, 720, 1.0f);
    expect(frameLayout(1024, 768, 1.0f).compact && frameLayout(1080, 1920, 1.0f).compact && frameLayout(800, 600, 1.0f).compact,
           "a 4:3, portrait or small display gets the compact layout", 1024, 768, 1.0f);

    expect(std::fabs(frameLayout(1280, 720, 1.0f).cardHeight - 400.0f) < 0.01f, "a display with no room to spare keeps the card at its design height", 1280, 720, 1.0f);
    const FrameLayout tall = frameLayout(1920, 1080, 1.25f);
    expect(tall.cardHeight > 400.0f * tall.scale + 1.0f && tall.card().bottom() <= 1080.0f - 16.0f * tall.scale + 0.01f,
           "a display with room below the card gives the card that room, above the bottom edge", 1920, 1080, 1.25f);
    expect(frameLayout(3840, 2160, 1.0f).cardHeight <= 600.0f + 0.01f, "the card grows to 600 at most, however tall the display", 3840, 2160, 1.0f);

    if (failures == 0)
        std::printf("frame layout: every case holds\n");
    return failures == 0 ? 0 : 1;
}
