#include "logger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <system_error>

#include <sstream>

namespace vkBasalt
{
    Logger Logger::s_instance;

    Logger::Logger() : m_minLevel(getMinLogLevel())
    {
        if (m_minLevel != LogLevel::None)
        {
            std::string filename = getFileName();
            if (filename == "stderr")
            {
                m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(&std::cerr, [](std::ostream*) {});
            }
            else if (filename == "stdout")
            {
                m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(&std::cout, [](std::ostream*) {});
            }
            else
            {
                // a game launch is several Vulkan processes (launcher, wine preloader, game) writing this one file
                auto* file = new std::ofstream(filename, std::ios::app);
                if (file->is_open())
                {
                    m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(file, [](std::ostream* os) { delete os; });
                }
                else
                {
                    delete file;
                    std::cerr << "vkDaedalus warn:  cannot open VKDAEDALUS_LOG_FILE " << filename << ", logging to stderr" << std::endl;
                    m_outStream = std::unique_ptr<std::ostream, std::function<void(std::ostream*)>>(&std::cerr, [](std::ostream*) {});
                }
            }
        }
    }

    Logger::~Logger()
    {
    }

    void Logger::trace(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Trace, message);
    }

    void Logger::trace(const char* message)
    {
        if (s_instance.m_minLevel.load(std::memory_order_relaxed) > LogLevel::Trace)
            return;
        s_instance.emitMsg(LogLevel::Trace, message);
    }

    void Logger::debug(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Debug, message);
    }

    void Logger::info(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Info, message);
    }

    void Logger::warn(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Warn, message);
    }

    void Logger::err(const std::string& message)
    {
        s_instance.emitMsg(LogLevel::Error, message);
    }

    void Logger::log(LogLevel level, const std::string& message)
    {
        s_instance.emitMsg(level, message);
    }

    void Logger::emitMsg(LogLevel level, const std::string& message)
    {
        const bool remembered = m_historyEnabled.load(std::memory_order_relaxed) && (level >= LogLevel::Info || level >= m_minLevel);
        if (level < m_minLevel && level < LogLevel::Warn && !remembered)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);

        std::deque<LogEntry>* kept = level >= LogLevel::Warn ? &m_problems : remembered ? &m_history : nullptr;
        if (kept)
        {
            kept->push_back({level, message, ++m_sequence});
            if (kept->size() > MAX_HISTORY_SIZE)
                kept->pop_front();
        }

        if (level >= m_minLevel)
        {
            static std::array<const char*, 5> s_prefixes = {
                {"vkDaedalus trace: ", "vkDaedalus debug: ", "vkDaedalus info:  ", "vkDaedalus warn:  ", "vkDaedalus err:   "}};

            const char* prefix = s_prefixes.at(static_cast<uint32_t>(level));

            std::stringstream stream(message);
            std::string       line;

            std::string written;
            while (std::getline(stream, line, '\n'))
            {
                *m_outStream << prefix << line << '\n';
                written += prefix + line + '\n';
            }

            if (level >= LogLevel::Warn)
                m_outStream->flush();
            writeStateLog(written, level >= LogLevel::Warn);
        }
    }

    std::string Logger::stateFolder()
    {
        if (const char* state = getenv("XDG_STATE_HOME"); state != nullptr && *state != '\0')
            return std::string(state) + "/vkdaedalus";
        if (const char* home = getenv("HOME"); home != nullptr && *home != '\0')
            return std::string(home) + "/.local/state/vkdaedalus";
        return "";
    }

    std::string Logger::stateLogPath()
    {
        const std::string folder = stateFolder();
        return folder.empty() ? "" : folder + "/vkdaedalus.log";
    }

    bool Logger::openStateLog()
    {
        const std::string path = stateLogPath();
        std::error_code   problem;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), problem);
        if (!problem && std::filesystem::exists(path, problem) && std::filesystem::file_size(path, problem) > stateLogBound)
            std::filesystem::rename(path, path + ".1", problem);
        m_stateLog = std::make_unique<std::ofstream>(path, std::ios::app);
        if (problem || !m_stateLog->is_open())
        {
            *m_outStream << "vkDaedalus warn:  cannot write the log file " << path << (problem ? ": " + problem.message() : "") << ", so it logs only here\n";
            m_stateLog.reset();
            return false;
        }
        std::error_code unsized;
        const std::uintmax_t size = std::filesystem::file_size(path, unsized);
        m_stateLogBytes = unsized ? 0 : size;
        return true;
    }

    void Logger::writeStateLog(const std::string& text, bool flush)
    {
        if (!m_stateLogTried)
        {
            m_stateLogTried       = true;
            const char* named     = getenv("VKDAEDALUS_LOG_FILE");
            if ((named != nullptr && *named != '\0') || stateLogPath().empty() || !openStateLog())
                return;
        }
        if (!m_stateLog)
            return;
        *m_stateLog << text;
        m_stateLogBytes += text.size();
        if (flush)
            m_stateLog->flush();
        if (m_stateLogBytes > stateLogBound)
        {
            m_stateLog->flush();
            m_stateLog.reset();
            if (!openStateLog())
                return;
        }
    }

    std::vector<LogEntry> Logger::getHistory()
    {
        std::lock_guard<std::mutex> lock(s_instance.m_mutex);
        std::vector<LogEntry>       merged;
        merged.reserve(s_instance.m_history.size() + s_instance.m_problems.size());
        std::merge(s_instance.m_history.begin(), s_instance.m_history.end(), s_instance.m_problems.begin(), s_instance.m_problems.end(),
                   std::back_inserter(merged), [](const LogEntry& a, const LogEntry& b) { return a.sequence < b.sequence; });
        return merged;
    }

    void Logger::clearHistory()
    {
        std::lock_guard<std::mutex> lock(s_instance.m_mutex);
        s_instance.m_history.clear();
        s_instance.m_problems.clear();
    }

    void Logger::setHistoryEnabled(bool enabled)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(s_instance.m_mutex);
            changed = s_instance.m_historyEnabled.exchange(enabled) != enabled;
            if (!enabled)
                s_instance.m_history.clear();
        }
        if (changed)
            s_instance.emitMsg(LogLevel::Debug, enabled ? "debug log history on" : "debug log history off");
    }

    bool Logger::isHistoryEnabled()
    {
        return s_instance.m_historyEnabled.load(std::memory_order_relaxed);
    }

    const char* Logger::levelName(LogLevel level)
    {
        static std::array<const char*, 5> names = {{"TRACE", "DEBUG", "INFO", "WARN", "ERROR"}};
        uint32_t idx = static_cast<uint32_t>(level);
        if (idx < names.size())
            return names[idx];
        return "UNKNOWN";
    }

    std::optional<LogLevel> Logger::parseLevel(const std::string& name)
    {
        const std::array<std::pair<const char*, LogLevel>, 6> logLevels = {{
            {"trace", LogLevel::Trace},
            {"debug", LogLevel::Debug},
            {"info", LogLevel::Info},
            {"warn", LogLevel::Warn},
            {"error", LogLevel::Error},
            {"none", LogLevel::None},
        }};
        for (const auto& pair : logLevels)
            if (name == pair.first)
                return pair.second;
        return std::nullopt;
    }

    bool Logger::levelSetByEnvironment()
    {
        const char* envVar = getenv("VKDAEDALUS_LOG_LEVEL");
        return envVar != nullptr && parseLevel(envVar).has_value();
    }

    void Logger::useSettingsLevel(const std::string& name)
    {
        if (levelSetByEnvironment())
            return;
        if (const std::optional<LogLevel> level = parseLevel(name))
            s_instance.m_minLevel.store(*level);
    }

    LogLevel Logger::getMinLogLevel()
    {
        const char* envVar = getenv("VKDAEDALUS_LOG_LEVEL");
        if (const std::optional<LogLevel> level = parseLevel(envVar ? envVar : ""))
            return *level;

        return LogLevel::Info;
    }

    std::string Logger::getFileName()
    {
        const char* envVar = getenv("VKDAEDALUS_LOG_FILE");

        std::string filename = envVar ? envVar : "";

        if (filename.empty())
        {
            filename = "stderr";
        }

        return filename;
    }

}
