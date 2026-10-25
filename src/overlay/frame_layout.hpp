#ifndef FRAME_LAYOUT_HPP_INCLUDED
#define FRAME_LAYOUT_HPP_INCLUDED

#include <algorithm>
#include <array>

namespace vkBasalt
{
    struct LayoutRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        float right() const { return x + w; }
        float bottom() const { return y + h; }
    };

    struct FrameLayout
    {
        float                     scale   = 1.0f;
        bool                      compact = false;
        LayoutRect                display;
        LayoutRect                topBar;
        LayoutRect                warnings;
        std::array<LayoutRect, 3> leftTiles;
        std::array<LayoutRect, 3> sectionTiles;
        std::array<LayoutRect, 2> rightTiles;
        std::array<LayoutRect, 3> iconTiles;
        float                     contentLeft  = 0.0f;
        float                     contentWidth = 0.0f;
        float                     cardTop      = 0.0f;
        float                     cardHeight   = 0.0f;

        LayoutRect card() const
        {
            return {contentLeft, cardTop, contentWidth, cardHeight};
        }
    };

    struct FrameDesign
    {
        static constexpr float width        = 1280.0f;
        static constexpr float height       = 720.0f;
        static constexpr float topBar       = 56.0f;
        static constexpr float contentWidth = 980.0f;
        static constexpr float tilesTop     = 88.0f;
        static constexpr float sectionTile  = 176.0f;
        static constexpr float sectionTileH = 200.0f;
        static constexpr float tileGap      = 2.0f;
        static constexpr float columnWidth  = 200.0f;
        static constexpr float columnItem   = 64.0f;
        static constexpr float columnGap    = 4.0f;
        static constexpr float cardTop      = 304.0f;
        static constexpr float cardHeight   = 400.0f;
        static constexpr float cardRoom     = 600.0f;
        static constexpr float edge         = 16.0f;
    };

    struct CompactDesign
    {
        static constexpr float width        = 880.0f;
        static constexpr float contentWidth = 840.0f;
        static constexpr float firstRowH    = 56.0f;
        static constexpr float secondRowH   = 64.0f;
        static constexpr float gap          = 4.0f;
        static constexpr float tilesTop     = FrameDesign::topBar + FrameDesign::edge;
        static constexpr float cardTop      = tilesTop + firstRowH + gap + secondRowH + FrameDesign::edge;
        static constexpr float height       = cardTop + FrameDesign::cardHeight + FrameDesign::edge;
    };

    inline float wantedScale(float desktopScale)
    {
        return desktopScale > 0.0f ? desktopScale : 1.0f;
    }

    inline float fittedFrameScale(float displayWidth, float displayHeight, float desktopScale)
    {
        const float fits = std::min(displayWidth / FrameDesign::width, displayHeight / FrameDesign::height);
        return std::max(0.01f, std::min(wantedScale(desktopScale), fits));
    }

    inline float fittedCompactScale(float displayWidth, float displayHeight, float desktopScale)
    {
        const float fits = std::min(displayWidth / CompactDesign::width, displayHeight / CompactDesign::height);
        return std::max(0.01f, std::min(wantedScale(desktopScale), fits));
    }

    inline bool compactFrameReadsBetter(float displayWidth, float displayHeight, float desktopScale)
    {
        return fittedCompactScale(displayWidth, displayHeight, desktopScale) > fittedFrameScale(displayWidth, displayHeight, desktopScale) * 1.15f;
    }

    inline FrameLayout compactFrameLayout(float displayWidth, float displayHeight, float desktopScale)
    {
        using D = FrameDesign;
        using C = CompactDesign;
        FrameLayout layout;
        const float s  = fittedCompactScale(displayWidth, displayHeight, desktopScale);
        layout.scale   = s;
        layout.compact = true;
        layout.display = {0.0f, 0.0f, displayWidth, displayHeight};
        layout.topBar  = {0.0f, 0.0f, displayWidth, D::topBar * s};

        const float left = (displayWidth - C::contentWidth * s) * 0.5f;
        const float top  = C::tilesTop * s;
        layout.warnings  = {left, layout.topBar.bottom(), C::contentWidth * s, top - layout.topBar.bottom()};
        const float firstW = (C::contentWidth - 2.0f * C::gap) / 3.0f;
        for (size_t i = 0; i < 3; i++)
            layout.leftTiles[i] = {left + i * (firstW + C::gap) * s, top, firstW * s, C::firstRowH * s};

        const float secondW = (C::contentWidth - 7.0f * C::gap) / 8.0f;
        const float secondY = top + (C::firstRowH + C::gap) * s;
        const auto  slot    = [&](size_t i) { return LayoutRect{left + i * (secondW + C::gap) * s, secondY, secondW * s, C::secondRowH * s}; };
        for (size_t i = 0; i < 3; i++)
            layout.sectionTiles[i] = slot(i);
        for (size_t i = 0; i < 2; i++)
            layout.rightTiles[i] = slot(3 + i);
        for (size_t i = 0; i < 3; i++)
            layout.iconTiles[i] = slot(5 + i);

        layout.contentLeft  = left;
        layout.contentWidth = C::contentWidth * s;
        layout.cardTop      = C::cardTop * s;
        layout.cardHeight   = std::max(0.0f, std::min(D::cardRoom * s, displayHeight - layout.cardTop - D::edge * s));
        return layout;
    }

    inline FrameLayout frameLayout(float displayWidth, float displayHeight, float desktopScale)
    {
        if (compactFrameReadsBetter(displayWidth, displayHeight, desktopScale))
            return compactFrameLayout(displayWidth, displayHeight, desktopScale);
        using D = FrameDesign;
        FrameLayout layout;
        const float s  = fittedFrameScale(displayWidth, displayHeight, desktopScale);
        layout.scale   = s;
        layout.display = {0.0f, 0.0f, displayWidth, displayHeight};
        layout.topBar  = {0.0f, 0.0f, displayWidth, D::topBar * s};

        const float left    = (displayWidth - D::contentWidth * s) * 0.5f;
        const float top     = D::tilesTop * s;
        layout.warnings     = {left, layout.topBar.bottom(), D::contentWidth * s, top - layout.topBar.bottom()};
        const float centreW = 3.0f * D::sectionTile * s + 2.0f * D::tileGap * s;
        const float centreX = (displayWidth - centreW) * 0.5f;
        for (size_t i = 0; i < 3; i++)
        {
            layout.leftTiles[i]    = {left, top + i * (D::columnItem + D::columnGap) * s, D::columnWidth * s, D::columnItem * s};
            layout.sectionTiles[i] = {centreX + i * (D::sectionTile + D::tileGap) * s, top, D::sectionTile * s, D::sectionTileH * s};
        }
        const float rightX = left + (D::contentWidth - D::columnWidth) * s;
        for (size_t i = 0; i < 2; i++)
            layout.rightTiles[i] = {rightX, top + i * (D::columnItem + D::columnGap) * s, D::columnWidth * s, D::columnItem * s};
        const float iconY = top + 2.0f * (D::columnItem + D::columnGap) * s;
        const float iconW = (D::columnWidth - 2.0f * D::columnGap) * s / 3.0f;
        for (size_t i = 0; i < 3; i++)
            layout.iconTiles[i] = {rightX + i * (iconW + D::columnGap * s), iconY, iconW, D::columnItem * s};

        layout.contentLeft  = left;
        layout.contentWidth = D::contentWidth * s;
        layout.cardTop      = D::cardTop * s;
        layout.cardHeight   = std::max(0.0f, std::min(D::cardRoom * s, displayHeight - layout.cardTop - D::edge * s));
        return layout;
    }
}

#endif
