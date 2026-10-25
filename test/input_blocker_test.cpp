#include "input_blocker.hpp"
#include "logger.hpp"
#include "wayland_display.hpp"
#include "game_input_hook.hpp"

#include <cstdio>
#include <vector>

namespace vkBasalt
{

    static bool              hookWrapped    = true;
    static bool              waylandSession = true;
    static bool              x11Hooked      = false;
    static std::vector<bool> hookBlocks;

    bool isWayland()
    {
        return waylandSession;
    }

    bool gameInputHooked()
    {
        return hookWrapped;
    }

    bool gameX11InputHooked()
    {
        return x11Hooked;
    }

    void setGameInputBlocked(bool blocked)
    {
        hookBlocks.push_back(blocked);
    }

    void* getKeyboardDisplay()
    {
        return nullptr;
    }
}

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    using namespace vkBasalt;

    setInputBlocked(true);
    expect(isInputBlocked(), "an open overlay blocks the game's events, with no setting to ask for it");
    expect(hookBlocks == std::vector<bool>{true}, "the input hook is told to block when the overlay opens");

    setInputBlocked(false);
    expect(!isInputBlocked(), "closing the overlay releases the block it holds");
    expect(hookBlocks == std::vector<bool>{true, false}, "the input hook forwards again when the overlay closes");

    setInputBlocked(false);
    expect(hookBlocks.size() == 2, "a release with nothing blocked tells the hook nothing");

    setInputBlocked(true);
    setInputBlocked(false);
    expect(hookBlocks == std::vector<bool>{true, false, true, false}, "every block is paired with one release");

    hookWrapped = false;
    setInputBlocked(true);
    expect(!isInputBlocked(), "blocking is refused while the input hook wrapped no game listener");
    expect(hookBlocks.size() == 4, "a refused block tells the hook nothing");

    waylandSession = false;
    setInputBlocked(true);
    expect(!isInputBlocked(), "on X11 a grab that cannot be taken is not reported as blocking");
    expect(!inputFocusLost(), "with no grab held there is no focus to lose");
    expect(hookBlocks.size() == 4, "a refused X11 block tells the hook nothing");

    x11Hooked = true;
    setInputBlocked(true);
    expect(isInputBlocked() && hookBlocks == std::vector<bool>{true, false, true, false, true},
           "on X11 with the input hook preloaded the hook holds the game's events, with no grab to be refused");
    setInputBlocked(false);
    expect(!isInputBlocked() && hookBlocks == std::vector<bool>{true, false, true, false, true, false}, "and closing the overlay has the hook forward again");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the block is released on every path that ends it\n");
    return 0;
}
