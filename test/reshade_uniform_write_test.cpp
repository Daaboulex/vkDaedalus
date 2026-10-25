#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "reshade_uniforms.hpp"

using namespace vkBasalt;

namespace
{
    reshadefx::uniform floatUniform(uint32_t offset, unsigned rows)
    {
        reshadefx::uniform u;
        u.name        = "test";
        u.offset      = offset;
        u.size        = rows * 4;
        u.type.base   = reshadefx::type::t_float;
        u.type.rows   = rows;
        u.type.cols   = 1;
        return u;
    }

    struct WritingUniform : ReshadeUniform
    {
        using ReshadeUniform::ReshadeUniform;
        void update(void*, size_t) override {}
        bool write(void* buffer, size_t size, const float* values, size_t count) { return writeValues(buffer, size, values, count); }
    };

    float wordAt(const std::vector<uint8_t>& buffer, size_t word)
    {
        float value;
        std::memcpy(&value, buffer.data() + word * 4, 4);
        return value;
    }
}

int main()
{
    const float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    std::vector<uint8_t> buffer(16, 0);
    WritingUniform scalar(floatUniform(4, 1));
    if (!scalar.write(buffer.data(), buffer.size(), values, 1) || wordAt(buffer, 1) != 1.0f || wordAt(buffer, 0) != 0.0f)
        return std::puts("a scalar at offset 4 must land in word 1 and nowhere else"), 1;

    std::fill(buffer.begin(), buffer.end(), 0);
    WritingUniform pair(floatUniform(0, 2));
    if (!pair.write(buffer.data(), buffer.size(), values, 4) || wordAt(buffer, 1) != 2.0f || wordAt(buffer, 2) != 0.0f)
        return std::puts("a float2 given four values must write two and leave the next uniform alone"), 1;

    std::vector<uint8_t> exact(16, 0);
    WritingUniform straddling(floatUniform(12, 2));
    if (straddling.write(exact.data(), exact.size(), values, 2) || wordAt(exact, 3) != 0.0f)
        return std::puts("a uniform running past the end of the buffer must be refused and write nothing"), 1;

    WritingUniform beyond(floatUniform(32, 1));
    if (beyond.write(exact.data(), exact.size(), values, 1))
        return std::puts("a uniform beyond the buffer must be refused"), 1;

    std::puts("ReShade uniform writes: all assertions passed");
    return 0;
}
