#include <algorithm>
#include "logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace vkBasalt;
namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static std::string contents(const fs::path& path)
{
    std::ifstream      file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;
    const fs::path dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::fflush(nullptr);
    const pid_t named = fork();
    if (named == 0)
    {
        setenv("VKDAEDALUS_LOG_FILE", (dir / "named.log").c_str(), 1);
        setenv("XDG_STATE_HOME", (dir / "named-state").c_str(), 1);
        Logger::info("a line for the named file");
        _exit(0);
    }
    int status = 0;
    waitpid(named, &status, 0);
    expect(!fs::exists(dir / "named-state" / "vkdaedalus" / "vkdaedalus.log"), "with VKDAEDALUS_LOG_FILE set, no second log file is written");

    unsetenv("VKDAEDALUS_LOG_FILE");
    setenv("XDG_STATE_HOME", (dir / "state").c_str(), 1);
    const fs::path log = dir / "state" / "vkdaedalus" / "vkdaedalus.log";
    Logger::info("the first line");
    Logger::warn("a warning");
    const std::string first = contents(log);
    expect(first.find("vkDaedalus info:  the first line\n") != std::string::npos && first.find("vkDaedalus warn:  a warning\n") != std::string::npos,
           "with no VKDAEDALUS_LOG_FILE the log also goes to $XDG_STATE_HOME/vkdaedalus/vkdaedalus.log, the same lines as stderr");

    const std::string block(64 * 1024, 'x');
    for (std::uintmax_t written = 0; written <= Logger::stateLogBound + block.size(); written += block.size())
        Logger::info(block);
    Logger::warn("after the bound");
    expect(fs::exists(log.string() + ".1"), "a log past its bound moves to vkdaedalus.log.1");
    expect(fs::file_size(log) < Logger::stateLogBound && contents(log).find("after the bound") != std::string::npos,
           "and the log starts again under the bound with the newest lines");
    expect(fs::file_size(log.string() + ".1") <= Logger::stateLogBound + 2 * block.size(), "the moved log holds about one bound, no more");

    Logger::setHistoryEnabled(true);
    Logger::trace("a trace line nobody asked for");
    Logger::info("a line the Debug card shows");
    const std::vector<LogEntry> history = Logger::getHistory();
    const auto has = [&](const std::string& text) {
        return std::any_of(history.begin(), history.end(), [&](const LogEntry& entry) { return entry.message == text; });
    };
    expect(has("a line the Debug card shows") && !has("a trace line nobody asked for"),
           "at the info level the Debug card's history keeps info and up, so trace lines cannot push them out");
    Logger::setHistoryEnabled(false);
    expect(!Logger::isHistoryEnabled(), "the history can be switched off again");
    return failures ? 1 : 0;
}
