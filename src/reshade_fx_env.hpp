#pragma once


#include <string>

#include "reshade/effect_codegen.hpp"
#include "reshade/effect_preprocessor.hpp"
#include "reshade_fx_version.hpp"

namespace vkBasalt
{
    inline reshadefx::codegen* createLayerSpirvCodegen(bool uniformsToSpecConstants, bool relaxMinPrecision)
    {
        const bool vulkanSemantics  = true;
        const bool debugInfo        = true;
        const bool enable16BitTypes = false;
        const bool flipVertexY      = true;
        return reshadefx::create_codegen_spirv(
            vulkanSemantics, debugInfo, uniformsToSpecConstants, enable16BitTypes, flipVertexY, relaxMinPrecision);
    }

    inline void addReshadeBaseMacros(reshadefx::preprocessor& pp, bool liveUniforms)
    {
        pp.add_macro_definition("__RESHADE__", std::to_string(VKBASALT_RESHADE_FX_VERSION));
        pp.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", liveUniforms ? "0" : "1");
        pp.add_macro_definition("__RENDERER__", "0x20000");

        pp.append_string("#define tex2Doffset(s, coords, offset) tex2D(s, coords, offset)\n"
                         "#define tex2Dlodoffset(s, coords, offset) tex2Dlod(s, coords, offset)\n"
                         "#define tex2DgatherRoffset(s, coords, offset) tex2DgatherR(s, coords, offset)\n"
                         "#define tex2DgatherGoffset(s, coords, offset) tex2DgatherG(s, coords, offset)\n"
                         "#define tex2DgatherBoffset(s, coords, offset) tex2DgatherB(s, coords, offset)\n"
                         "#define tex2DgatherAoffset(s, coords, offset) tex2DgatherA(s, coords, offset)\n"
                         "#define tex2Dgather(s, t, c) tex2Dgather##c(s, t)\n"
                         "#define tex2Dgatheroffset(s, t, o, c) tex2Dgather##c(s, t, o)\n"
                         "#define tex2Dgather0 tex2DgatherR\n"
                         "#define tex2Dgather1 tex2DgatherG\n"
                         "#define tex2Dgather2 tex2DgatherB\n"
                         "#define tex2Dgather3 tex2DgatherA\n"
                         "#define ddx_fine(x) ddx(x)\n"
                         "#define ddy_fine(x) ddy(x)\n"
                         "#define ddx_coarse(x) ddx(x)\n"
                         "#define ddy_coarse(x) ddy(x)\n");
    }
}
