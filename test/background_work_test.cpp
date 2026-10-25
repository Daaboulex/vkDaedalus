#include "overlay/effect_compile_jobs.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

using namespace vkBasalt;

int main()
{
    int fds[2];
    if (pipe(fds) != 0)
    {
        std::printf("FAIL: no pipe\n");
        return 1;
    }
    const pid_t child = fork();
    if (child < 0)
    {
        std::printf("FAIL: no fork\n");
        return 1;
    }
    if (child == 0)
    {
        close(fds[0]);
        const int                out     = fds[1];
        static std::atomic<bool> started = false;
        EffectCompileJobs*       jobs    = new EffectCompileJobs();
        jobs->start({{{"exiting", "dls_fx", "/fx/exiting.fx"}}, std::nullopt},
                    {[out] {
                         started = true;
                         std::this_thread::sleep_for(std::chrono::milliseconds(200));
                         if (write(out, "first\n", 6) != 6)
                             std::abort();
                     },
                     [out] {
                         if (write(out, "second\n", 7) != 7)
                             std::abort();
                     }});
        while (!started)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::exit(0);
    }
    close(fds[1]);
    std::string seen;
    char        buffer[64];
    ssize_t     got = 0;
    while ((got = read(fds[0], buffer, sizeof(buffer))) > 0)
        seen.append(buffer, static_cast<size_t>(got));
    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        std::printf("FAIL: a process exiting during a background compile did not exit cleanly (status %d)\n", status);
        return 1;
    }
    if (seen != "first\n")
    {
        std::printf("FAIL: at exit the running compile must finish and no other may start, got \"%s\"\n", seen.c_str());
        return 1;
    }
    std::printf("OK: a process exiting mid-compile waits for the compile running and starts no other\n");
    return 0;
}
