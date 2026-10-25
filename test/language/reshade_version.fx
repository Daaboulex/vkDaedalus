#if __RESHADE__ != 60500
#error "__RESHADE__ is not the 60500 the layer claims in reshade_fx_version.hpp"
#endif

float4 VersionVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 VersionPS(float4 pos : SV_Position) : SV_Target
{
    return float4(__RESHADE__ / 100000.0, 0.0, 0.0, 1.0);
}

technique ReshadeVersion
{
    pass
    {
        VertexShader = VersionVS;
        PixelShader  = VersionPS;
    }
}
