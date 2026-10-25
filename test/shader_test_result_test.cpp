#include "shader_test_result.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

int main()
{
    const vkBasalt::ShaderTestResult plain{"cas", "/shaders/cas.fx", true, false, false, ""};
    const vkBasalt::ShaderTestResult depth{"ssao", "/shaders/ssao.fx", true, true, false, ""};
    const vkBasalt::ShaderTestResult broken{"broken", "/shaders/broken.fx", false, true, false, "error X3000"};

    expect(vkBasalt::shaderTestAllowed(plain, true) && vkBasalt::shaderTestAllowed(plain, false), "a shader that compiled and reads no depth is allowed either way");
    expect(!vkBasalt::shaderTestAllowed(depth, true), "with Safe Anti-Cheat on, a shader reading depth is not allowed");
    expect(vkBasalt::shaderTestAllowed(depth, false),
           "with Safe Anti-Cheat off, a shader reading depth is allowed, so the Allowed list holds what its count says");
    expect(!vkBasalt::shaderTestAllowed(broken, false) && !vkBasalt::shaderTestAllowed(broken, true), "a shader that failed is never allowed");

    return failures == 0 ? 0 : 1;
}
