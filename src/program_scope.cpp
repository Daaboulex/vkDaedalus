#include "program_scope.hpp"

#include <cctype>
#include <cstdlib>

#include "anticheat_files.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    namespace
    {
        bool endsWithExe(const std::string& name)
        {
            if (name.size() < 4)
                return false;
            std::string tail = name.substr(name.size() - 4);
            for (char& c : tail)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return tail == ".exe";
        }

        bool sameProgram(const std::string& a, const std::string& b)
        {
            if (!endsWithExe(a) || !endsWithExe(b))
                return a == b;
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        std::string gameFolderOf(const std::string& executable)
        {
            if (executable.empty() || executable[0] != '/' || inSystemPlace(executable))
                return "";
            return gameFolderFor(executable);
        }

        bool outsideTheGameItStarted()
        {
            const char* named = std::getenv("VKDAEDALUS_RUN_PROGRAM");
            if (named == nullptr || *named == '\0')
                return false;
            const char*       namedExecutable = std::getenv("VKDAEDALUS_RUN_EXECUTABLE");
            const std::string namedFolder =
                gameFolderOf(namedExecutable ? unixPathOfWindowsPath(namedExecutable, winePrefixOfThisProcess()) : "");
            const std::string program = ConfigSerializer::detectGameName();
            std::string       problem;
            const std::string folder = gameFolderOf(programExecutable(problem));
            if (partOfTheGame(program, folder, named, namedFolder))
                return false;
            Logger::info(program + " is not the game vkdaedalus-run started (" + named + (namedFolder.empty() ? "" : ", in " + namedFolder)
                         + "), so vkDaedalus stays out of it");
            return true;
        }
    }

    bool partOfTheGame(const std::string& program, const std::string& folder, const std::string& namedProgram, const std::string& namedFolder)
    {
        if (namedProgram.empty() || sameProgram(program, namedProgram))
            return true;
        return !folder.empty() && folder == namedFolder;
    }

    bool programBypassed()
    {
        static const bool bypassed =
            outsideTheGameItStarted() || ConfigSerializer::getActiveProfile(ConfigSerializer::detectGameName()).kind == ProfileKind::Bypass;
        return bypassed;
    }
}
