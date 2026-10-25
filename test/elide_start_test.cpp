#include "overlay/elide_start.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static float codePoints(const std::string& text)
{
    float count = 0.0f;
    for (const char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            count += 1.0f;
    return count;
}

static std::string elided(const std::string& text, float room)
{
    return vkBasalt::elideStart(text, room, codePoints);
}

int main()
{
    const std::string path = "/home/user/games/lsfg";
    expect(elided(path, codePoints(path)) == path, "a text that fits is shown whole");
    expect(elided(path, 200.0f) == path, "a text in more room than it needs is shown whole");

    for (float room = 4.0f; room < codePoints(path); room += 1.0f)
    {
        const std::string shown = elided(path, room);
        const std::string at    = " in room " + std::to_string(static_cast<int>(room)) + ": " + shown;
        expect(shown.rfind("...", 0) == 0, "a cut text starts with ..." + at);
        expect(codePoints(shown) <= room, "a cut text fits its room" + at);
        expect(codePoints(shown) == room, "a cut text keeps as much of the end as fits" + at);
        expect(path.size() >= shown.size() - 3 && path.compare(path.size() - (shown.size() - 3), std::string::npos, shown, 3) == 0,
               "a cut text keeps the end of the text" + at);
    }

    const std::string wide = "/home/j\xc3\xbcrgen/spi\xc3\xa9le/\xe6\xb8\xb8\xe6\x88\x8f";
    for (float room = 4.0f; room < codePoints(wide); room += 1.0f)
    {
        const std::string shown = elided(wide, room);
        const std::string at    = " in room " + std::to_string(static_cast<int>(room));
        expect(shown.size() > 3 && (static_cast<unsigned char>(shown[3]) & 0xC0) != 0x80, "a cut never starts inside a character" + at);
        expect(codePoints(shown) == room, "a cut text of wide characters keeps as much as fits" + at);
    }

    if (failures != 0)
        return 1;
    std::printf("elide-start: every check passed\n");
    return 0;
}
