#include "launch_notice.hpp"

#include <cstdio>
#include <cstdlib>

using namespace vkBasalt;

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
    expect(launchNoticeDue(LaunchNotice::EveryLaunch, false), "every launch shows the notice on a game seen before");
    expect(launchNoticeDue(LaunchNotice::FirstLaunch, true), "first launch shows the notice on a new game");
    expect(!launchNoticeDue(LaunchNotice::FirstLaunch, false), "first launch shows nothing on a game seen before");
    expect(!launchNoticeDue(LaunchNotice::Never, true), "never shows nothing, even on a new game");

    for (const LaunchNotice notice : {LaunchNotice::EveryLaunch, LaunchNotice::FirstLaunch, LaunchNotice::Never})
        expect(launchNoticeFrom(launchNoticeWord(notice)) == notice, "a launch notice choice reads back as its own word");
    expect(!launchNoticeFrom("sometimes"), "a word that is no choice is refused");

    unsetenv("VKDAEDALUS_LAUNCH_NOTICE");
    expect(!launchNoticeFromEnvironment(), "with VKDAEDALUS_LAUNCH_NOTICE unset the saved choice applies");
    setenv("VKDAEDALUS_LAUNCH_NOTICE", "never", 1);
    expect(launchNoticeFromEnvironment() == LaunchNotice::Never, "VKDAEDALUS_LAUNCH_NOTICE=never sets this run's notice over the saved choice");
    setenv("VKDAEDALUS_LAUNCH_NOTICE", "sometimes", 1);
    expect(!launchNoticeFromEnvironment(), "a VKDAEDALUS_LAUNCH_NOTICE that is no choice leaves the saved choice in force");
    unsetenv("VKDAEDALUS_LAUNCH_NOTICE");

    const float duration = launchNoticeSeconds;
    expect(noticeLookAt(0.0f, duration).shown == 0.0f, "the notice starts off screen");
    expect(noticeLookAt(launchNoticeMoveSeconds, duration).shown == 1.0f, "the notice is fully in once it slid in");
    expect(noticeLookAt(duration / 2.0f, duration).shown == 1.0f, "the notice holds still between its moves");
    expect(noticeLookAt(duration - 0.01f, duration).shown < 0.01f, "the notice is out by its end");
    expect(noticeLookAt(duration, duration).done, "the notice is done at its duration");
    expect(!noticeLookAt(duration - 0.01f, duration).done, "the notice is not done before its duration");
    expect(noticeLookAt(1.0f, duration).remaining > noticeLookAt(3.0f, duration).remaining, "the countdown under the notice runs out over time");

    if (failures != 0)
        return 1;
    std::printf("launch-notice: every check passed\n");
    return 0;
}
