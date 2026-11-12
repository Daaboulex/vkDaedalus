#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "reshade_uniforms.hpp"

using namespace vkBasalt;

namespace
{
    reshadefx::type scalarOf(reshadefx::type::datatype base)
    {
        reshadefx::type type;
        type.base = base;
        type.rows = 1;
        type.cols = 1;
        return type;
    }

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
        void update(void*, size_t, const ReshadeFrame&) override {}
        bool write(void* buffer, size_t size, const float* values, size_t count)
        {
            uint32_t words[4] = {};
            for (size_t i = 0; i < count; i++)
                words[i] = reshadeWordFromFloat(uniformInfo.type, values[i]);
            return writeWords(buffer, size, words, count);
        }
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

    const reshadefx::type asFloat = scalarOf(reshadefx::type::t_float);
    const reshadefx::type asInt   = scalarOf(reshadefx::type::t_int);
    const reshadefx::type asUint  = scalarOf(reshadefx::type::t_uint);
    const reshadefx::type asBool  = scalarOf(reshadefx::type::t_bool);
    if (std::bit_cast<float>(reshadeWordFromUint(asFloat, 119)) != 119.0f)
        return std::puts("a frame count written to a float uniform must read as that number, as ReShade converts it"), 1;
    if (reshadeWordFromUint(asUint, 119) != 119 || reshadeWordFromInt(asInt, -3) != static_cast<uint32_t>(-3))
        return std::puts("an integer written to an integer uniform must keep its bits"), 1;
    if (reshadeWordFromFloat(asInt, 2.7f) != 2 || reshadeWordFromFloat(asInt, -2.7f) != static_cast<uint32_t>(-2))
        return std::puts("a float written to an integer uniform must be truncated toward zero, as static_cast does"), 1;
    if (std::bit_cast<float>(reshadeWordFromBool(asFloat, true)) != 1.0f || reshadeWordFromBool(asBool, true) != 1 || reshadeWordFromBool(asInt, false) != 0)
        return std::puts("a bool must be written as 1.0 or 0.0 to a float uniform and as 1 or 0 otherwise"), 1;
    if (std::bit_cast<float>(reshadeWordFromInt(asFloat, -5)) != -5.0f)
        return std::puts("a signed value written to a float uniform must keep its sign"), 1;
    if (reshadeFloatOfWord(asInt, static_cast<uint32_t>(-3)) != -3.0f || reshadeFloatOfWord(asUint, 0xFFFFFFFFu) != 4294967295.0f
        || reshadeFloatOfWord(asFloat, std::bit_cast<uint32_t>(0.25f)) != 0.25f)
        return std::puts("a stored value must read back as a float by its declared type"), 1;
    if (!reshadeBoolOfWord(asFloat, std::bit_cast<uint32_t>(0.5f)) || reshadeBoolOfWord(asFloat, std::bit_cast<uint32_t>(0.0f)) || !reshadeBoolOfWord(asBool, 1))
        return std::puts("a stored value must read back as a bool by whether it is zero"), 1;

    std::puts("ReShade uniform writes: all assertions passed");
    return 0;
}
