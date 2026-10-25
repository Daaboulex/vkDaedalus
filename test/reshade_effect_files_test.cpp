#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "reshade_effect_files.hpp"

namespace fs = std::filesystem;
using namespace vkBasalt;

int main()
{
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("vkbasalt-effect-files-test-" + std::to_string(::getpid()));
    if (ec)
        return std::puts("no temporary directory to test in"), 1;
    fs::remove_all(root, ec);
    fs::create_directories(root / "nested", ec);
    if (ec)
        return std::puts("could not create the test directory"), 1;
    for (const char* name : {"Upper.FX", "lower.fx", "Mixed.Fx", "notes.txt", "fx", "nested/inside.fx"})
        std::ofstream(root / name) << "technique T {}";
    fs::create_directory(root / "dir.fx", ec);

    const std::vector<fs::path> files = reshadeEffectFilesIn(root, ec);
    if (ec)
        return std::puts("listing a readable directory reported an error"), 1;
    std::vector<std::string> names;
    for (const auto& f : files)
        names.push_back(f.filename().string());
    const std::vector<std::string> expected = {"Mixed.Fx", "Upper.FX", "lower.fx"};
    if (names != expected)
        return std::puts("a directory lists exactly its .fx files in any case, sorted, without subdirectories or other files"), 1;

    reshadeEffectFilesIn(root / "missing", ec);
    if (!ec)
        return std::puts("a directory that cannot be read must report why"), 1;

    fs::remove_all(root, ec);
    std::puts("ReShade effect file discovery: all assertions passed");
    return 0;
}
