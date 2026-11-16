#include "overlay/frame_layout.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void expect(bool condition, const char* what, float width, float height, float scale, size_t sections = 3)
{
    if (condition)
        return;
    std::printf("FAIL: %s (display %.0fx%.0f, desktop scale %.2f, %zu section tiles)\n", what, width, height, scale, sections);
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
            for (size_t sections : {size_t{3}, size_t{4}})
        {
            const FrameLayout layout = frameLayout(w, h, scale, sections);
            expect(layout.sectionCount == sections, "the row holds the section tiles asked for", w, h, scale, sections);
            expect(layout.scale == frameScale(w, h, scale), "the frame scale does not depend on the number of section tiles", w, h, scale, sections);
            std::vector<LayoutRect> tiles;
            tiles.insert(tiles.end(), layout.leftTiles.begin(), layout.leftTiles.end());
            tiles.insert(tiles.end(), layout.sectionTiles.begin(), layout.sectionTiles.begin() + static_cast<std::ptrdiff_t>(sections));
            tiles.insert(tiles.end(), layout.rightTiles.begin(), layout.rightTiles.end());
            tiles.insert(tiles.end(), layout.iconTiles.begin(), layout.iconTiles.end());

            bool allInside = inside(layout.topBar, layout.display) && inside(layout.card(), layout.display);
            for (const LayoutRect& tile : tiles)
                allInside = allInside && inside(tile, layout.display);
            expect(allInside, "every part of the frame lies on the display", w, h, scale, sections);

            bool noOverlap = true;
            for (size_t i = 0; i < tiles.size(); i++)
            {
                for (size_t j = i + 1; j < tiles.size(); j++)
                    noOverlap = noOverlap && apart(tiles[i], tiles[j]);
                noOverlap = noOverlap && apart(tiles[i], layout.topBar) && apart(tiles[i], layout.card());
            }
            expect(noOverlap, "no tile overlaps another tile, the top bar or the card", w, h, scale, sections);

            float tilesLeft  = tiles.front().x;
            float tilesRight = tiles.front().right();
            for (const LayoutRect& tile : tiles)
            {
                tilesLeft  = std::min(tilesLeft, tile.x);
                tilesRight = std::max(tilesRight, tile.right());
            }
            expect(std::fabs(layout.card().x - tilesLeft) < 0.01f && std::fabs(layout.card().right() - tilesRight) < 0.01f,
                   "the card spans the tile row, edge to edge, on every page", w, h, scale, sections);
            expect(layout.cardHeight > 0.0f, "a card has room to show", w, h, scale, sections);
            expect(layout.scale >= fittedFrameScale(w, h, scale) - 0.0001f, "the layout chosen never reads smaller than the regular one", w, h, scale, sections);
            if (layout.compact)
                expect(fittedCompactScale(w, h, scale) > fittedFrameScale(w, h, scale), "the compact layout is chosen only when it reads larger", w, h, scale, sections);
            if (w >= 800.0f && h >= 600.0f && scale == 1.0f)
                expect(layout.scale >= 0.9f, "any display of at least 800 by 600 shows the overlay at 90% or more", w, h, scale, sections);
        }

    for (size_t sections : {size_t{3}, size_t{4}})
    {
        const FrameLayout wide = frameLayout(1920, 1080, 1.0f, sections);
        const LayoutRect& last = wide.sectionTiles[sections - 1];
        const float       left = wide.sectionTiles[0].x - wide.leftTiles[0].right();
        expect(std::fabs(left - (wide.rightTiles[0].x - last.right())) < 0.01f && left >= 16.0f - 0.01f,
               "the section tiles sit centred between the side columns, 16 px or more from each", 1920, 1080, 1.0f, sections);
    }
    expect(std::fabs(frameLayout(1920, 1080, 1.0f, 3).sectionTiles[0].w - 176.0f) < 0.01f, "three section tiles keep their design width", 1920, 1080, 1.0f, 3);

    const FrameLayout one  = frameLayout(3840, 2160, 1.0f, 3);
    const FrameLayout more = frameLayout(3840, 2160, 1.5f, 3);
    expect(std::fabs(more.sectionTiles[0].w / one.sectionTiles[0].w - 1.5f) < 0.001f && std::fabs(more.topBar.h / one.topBar.h - 1.5f) < 0.001f
               && std::fabs(more.card().w / one.card().w - 1.5f) < 0.001f,
           "on a display with room, the desktop scale scales the whole frame", 3840, 2160, 1.5f);

    const FrameLayout cramped = frameLayout(1280, 720, 2.0f, 3);
    const FrameLayout fitting = frameLayout(1280, 720, 1.0f, 3);
    expect(std::fabs(cramped.sectionTiles[0].w - fitting.sectionTiles[0].w) < 0.001f, "a scale too big for the display is reduced to what fits", 1280, 720, 2.0f);

    expect(!frameLayout(1280, 720, 1.0f, 3).compact && !frameLayout(1920, 1080, 1.5f, 3).compact && !frameLayout(3840, 2160, 2.0f, 3).compact,
           "a 16:9 display with room keeps the regular layout", 1280, 720, 1.0f);
    expect(frameLayout(1024, 768, 1.0f, 3).compact && frameLayout(1080, 1920, 1.0f, 3).compact && frameLayout(800, 600, 1.0f, 3).compact,
           "a 4:3, portrait or small display gets the compact layout", 1024, 768, 1.0f);

    expect(std::fabs(frameLayout(1280, 720, 1.0f, 3).cardHeight - 400.0f) < 0.01f, "a display with no room to spare keeps the card at its design height", 1280, 720, 1.0f);
    const FrameLayout tall = frameLayout(1920, 1080, 1.25f, 3);
    expect(tall.cardHeight > 400.0f * tall.scale + 1.0f && tall.card().bottom() <= 1080.0f - 16.0f * tall.scale + 0.01f,
           "a display with room below the card gives the card that room, above the bottom edge", 1920, 1080, 1.25f);
    expect(frameLayout(3840, 2160, 1.0f, 3).cardHeight <= 600.0f + 0.01f, "the card grows to 600 at most, however tall the display", 3840, 2160, 1.0f);

    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        const float gap = 8.0f * scale;
        bool        onGrid = true;
        for (float width = 30.0f * scale; width < 200.0f * scale; width += 0.37f)
        {
            const float snapped = widthOnGrid(width, gap);
            const float steps   = snapped / gap;
            onGrid              = onGrid && snapped >= width - 0.01f && snapped < width + gap && std::fabs(steps - std::round(steps)) < 0.001f;
        }
        if (!onGrid)
        {
            std::printf("FAIL: a choice tab is widened to the next whole number of gaps, never by a whole gap (scale %.2f)\n", scale);
            failures++;
        }
        const auto starts = [&](const std::vector<float>& widths) {
            std::vector<float> at;
            float              x = 0.0f;
            for (const float width : widths)
            {
                at.push_back(x);
                x += widthOnGrid(width * scale, gap) + gap;
            }
            return at;
        };
        bool apartOrAligned = true;
        for (const float a : starts({42.0f, 44.0f, 52.0f, 52.0f, 52.0f}))
            for (const float b : starts({96.0f, 34.0f, 36.0f}))
                apartOrAligned = apartOrAligned && (std::fabs(a - b) <= 0.75f || std::fabs(a - b) >= gap - 0.01f);
        if (!apartOrAligned)
        {
            std::printf("FAIL: the choices of two rows sharing a column start at the same place or a whole gap apart, never 2 px apart (scale %.2f)\n", scale);
            failures++;
        }
    }

    if (failures == 0)
        std::printf("frame layout: every case holds\n");
    return failures == 0 ? 0 : 1;
}
