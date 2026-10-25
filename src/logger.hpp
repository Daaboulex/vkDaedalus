#ifndef LOGGER_HPP_INCLUDED
#define LOGGER_HPP_INCLUDED

#include <array>
#include <atomic>
#include <optional>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <deque>

namespace vkBasalt
{

    enum class LogLevel : uint32_t
    {
        Trace = 0,
        Debug = 1,
        Info  = 2,
        Warn  = 3,
        Error = 4,
        None  = 5,
    };

    struct LogEntry
    {
        LogLevel    level;
        std::string message;
        uint64_t    sequence = 0;
    };

    class Logger
    {

    public:
        Logger();
        ~Logger();

        static void trace(const std::string& message);
        static void trace(const char* message);
        static void debug(const std::string& message);
        static void info(const std::string& message);
        static void warn(const std::string& message);
        static void err(const std::string& message);
        static void log(LogLevel level, const std::string& message);

        static LogLevel logLevel()
        {
            return s_instance.m_minLevel.load();
        }

        static std::optional<LogLevel> parseLevel(const std::string& name);
        static bool                    levelSetByEnvironment();
        static void                    useSettingsLevel(const std::string& name);

        static std::vector<LogEntry> getHistory();

        static void clearHistory();

        static void setHistoryEnabled(bool enabled);
        static bool isHistoryEnabled();

        static const char* levelName(LogLevel level);

        static std::string stateFolder();
        static std::string stateLogPath();
        static constexpr std::uintmax_t stateLogBound = 8u << 20;

    private:
        static Logger s_instance;
        static constexpr size_t MAX_HISTORY_SIZE = 1000;

        std::atomic<LogLevel> m_minLevel;

        std::mutex m_mutex;

        std::unique_ptr<std::ostream, std::function<void(std::ostream*)>> m_outStream;

        std::unique_ptr<std::ofstream> m_stateLog;
        std::uintmax_t                 m_stateLogBytes  = 0;
        bool                           m_stateLogTried  = false;

        std::deque<LogEntry> m_history;
        std::deque<LogEntry> m_problems;
        uint64_t             m_sequence = 0;
        std::atomic<bool>    m_historyEnabled{false};

        void emitMsg(LogLevel level, const std::string& message);
        bool openStateLog();
        void writeStateLog(const std::string& text, bool flush);

        static LogLevel getMinLogLevel();

        static std::string getFileName();
    };

}

#endif
