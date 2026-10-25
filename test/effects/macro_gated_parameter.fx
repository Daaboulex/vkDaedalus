#ifndef USE_GATE
#define USE_GATE 0
#endif

#if USE_GATE
uniform float GateBlue <
    ui_type = "slider";
    ui_label = "Gate Blue";
    ui_min = 0.0;
    ui_max = 1.0;
> = 0.0;
#endif

void MacroGatedVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 MacroGatedPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
#if USE_GATE
    return float4(0.0, 0.0, GateBlue, 1.0);
#else
    return float4(1.0, 0.0, 0.0, 1.0);
#endif
}

technique MacroGatedParameter
{
    pass
    {
        VertexShader = MacroGatedVS;
        PixelShader = MacroGatedPS;
    }
}
