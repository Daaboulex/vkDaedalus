#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
#define RESHADE_DEPTH_INPUT_IS_REVERSED 1
#endif

void GlobalDefinitionVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 GlobalDefinitionPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
#if RESHADE_DEPTH_INPUT_IS_REVERSED
    return float4(1.0, 0.0, 0.0, 1.0);
#else
    return float4(0.0, 0.0, 1.0, 1.0);
#endif
}

technique GlobalDefinition
{
    pass
    {
        VertexShader = GlobalDefinitionVS;
        PixelShader = GlobalDefinitionPS;
    }
}
