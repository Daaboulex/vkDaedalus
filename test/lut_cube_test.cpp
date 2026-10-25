#include "lut_cube.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace vkBasalt;
namespace fs = std::filesystem;

static int      failures = 0;
static fs::path dir;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static std::string cubeFile(const char* name, const std::string& content)
{
    const fs::path path = dir / name;
    std::ofstream(path, std::ios::binary) << content;
    return path.string();
}

static bool refused(const char* name, const std::string& content)
{
    try
    {
        LutCube lut(cubeFile(name, content));
        return false;
    }
    catch (const std::runtime_error&)
    {
        return true;
    }
}

static bool texel(const LutCube& lut, size_t index, int r, int g, int b)
{
    return lut.colorCube[index * 4 + 0] == r && lut.colorCube[index * 4 + 1] == g && lut.colorCube[index * 4 + 2] == b
           && lut.colorCube[index * 4 + 3] == 255;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);

    const std::string eight = "0 0 0\n"
                              "1.0 0 0\n"
                              "-0.0 0.5 1.0\r\n"
                              ".25 1 0\n"
                              "1.5 -0.5 0\n"
                              "0 0 1\n"
                              "0 1 1\n"
                              "1 1 1\n";

    try
    {
        LutCube lut(cubeFile("valid.cube", "TITLE \"probe\"\n# a comment\n\nLUT_3D_SIZE 2\n" + eight));
        expect(lut.size == 2, "LUT_3D_SIZE 2 gives a side of 2");
        expect(lut.colorCube.size() == 32, "a side of 2 holds 8 RGBA texels");
        if (lut.colorCube.size() == 32)
        {
            expect(texel(lut, 0, 0, 0, 0), "the first data line is texel 0");
            expect(texel(lut, 1, 255, 0, 0), "red runs fastest, so the second line is texel 1");
            expect(texel(lut, 2, 0, 128, 255), "a line starting with a minus sign is a data line, kept in place");
            expect(texel(lut, 3, 64, 255, 0), "a line starting with a dot is a data line, kept in place");
            expect(texel(lut, 4, 255, 0, 0), "values outside the domain clamp to its ends");
            expect(texel(lut, 7, 255, 255, 255), "the last line is the last texel");
        }
    }
    catch (const std::runtime_error& e)
    {
        std::printf("FAIL: a valid file was refused: %s\n", e.what());
        failures++;
    }

    try
    {
        LutCube lut(cubeFile("domain.cube", "DOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\nLUT_3D_SIZE 2\n" + eight));
        expect(lut.colorCube.size() == 32 && texel(lut, 7, 128, 128, 128), "values map through DOMAIN_MIN and DOMAIN_MAX");
    }
    catch (const std::runtime_error& e)
    {
        std::printf("FAIL: a file with a domain was refused: %s\n", e.what());
        failures++;
    }

    expect(refused("empty.cube", ""), "an empty file is refused");
    bool missingRefused = false;
    try
    {
        LutCube lut((dir / "never-written.cube").string());
    }
    catch (const std::runtime_error&)
    {
        missingRefused = true;
    }
    expect(missingRefused, "a missing file is refused");
    expect(refused("nine.cube", "LUT_3D_SIZE 2\n" + eight + "0 0 0\n"), "more data lines than the cube holds are refused");
    expect(refused("seven.cube", "LUT_3D_SIZE 2\n" + eight.substr(0, eight.rfind("1 1 1"))), "fewer data lines than the cube holds are refused");
    expect(refused("one.cube", "LUT_3D_SIZE 1\n0 0 0\n"), "a side of 1 is refused");
    expect(refused("huge.cube", "LUT_3D_SIZE 257\n"), "a side above 256 is refused");
    expect(refused("junk.cube", "LUT_3D_SIZE 2x\n" + eight), "a size with trailing junk is refused");
    expect(refused("pair.cube", "LUT_3D_SIZE 2\n0 0\n" + eight.substr(6)), "a data line with two numbers is refused");
    expect(refused("inverted.cube", "DOMAIN_MIN 1 1 1\nDOMAIN_MAX 0 0 0\nLUT_3D_SIZE 2\n" + eight), "an empty or inverted domain is refused");
    expect(refused("oned.cube", "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n"), "a 1D LUT is refused, not written into a cube that was never sized");
    expect(refused("early.cube", eight + "LUT_3D_SIZE 2\n"), "data before LUT_3D_SIZE is refused");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: cube LUTs parse strictly\n");
    return 0;
}
