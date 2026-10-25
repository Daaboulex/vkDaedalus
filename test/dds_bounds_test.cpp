#include "stb_image.h"
#include "stb_image_dds.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* what)
    {
        if (condition)
            return;
        std::printf("FAIL: %s\n", what);
        failures++;
    }

    std::vector<stbi_uc> ddsFile(uint32_t width, uint32_t height, uint32_t pixelFlags, uint32_t fourCC, uint32_t bitCount, size_t payload)
    {
        uint32_t header[31] = {};
        header[0]           = 124;
        header[1]           = 0x1 | 0x2 | 0x4 | 0x1000;
        header[2]           = height;
        header[3]           = width;
        header[18]          = 32;
        header[19]          = pixelFlags;
        header[20]          = fourCC;
        header[21]          = bitCount;
        header[26]          = 0x1000;

        std::vector<stbi_uc> file(4 + sizeof(header) + payload, 0);
        std::memcpy(file.data(), "DDS ", 4);
        std::memcpy(file.data() + 4, header, sizeof(header));
        return file;
    }

    bool refused(const std::vector<stbi_uc>& file)
    {
        int      x = 0, y = 0, comp = 0;
        stbi_uc* pixels = stbi_dds_load_from_memory(file.data(), static_cast<int>(file.size()), &x, &y, &comp, 4);
        if (pixels == nullptr)
            return true;
        stbi_image_free(pixels);
        return false;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const uint32_t dxt1 = 'D' | ('X' << 8) | ('T' << 16) | ('1' << 24);
    const uint32_t fourccFlag = 0x4;
    const uint32_t rgbFlag    = 0x40;

    int      x = 0, y = 0, comp = 0;
    stbi_uc* solid = stbi_dds_load(argv[1], &x, &y, &comp, 4);
    expect(solid != nullptr && x == 4 && y == 4, "the 4x4 fixture still decodes");
    if (solid != nullptr)
        stbi_image_free(solid);

    expect(refused(ddsFile(4, 4, rgbFlag, 0, 4096, 64)), "a pixel wider than four channels is refused");
    expect(refused(ddsFile(4, 4, rgbFlag, 0, 4, 64)), "a pixel narrower than one channel is refused");
    expect(refused(ddsFile(0, 4, rgbFlag, 0, 32, 64)), "a zero width is refused");
    expect(refused(ddsFile(65536, 65536, rgbFlag, 0, 32, 64)), "an uncompressed image whose byte count overflows is refused");
    expect(refused(ddsFile(32768, 32768, fourccFlag, dxt1, 0, 64)), "a compressed image whose byte count overflows is refused");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: DDS headers are bounded before anything is allocated\n");
    return 0;
}
