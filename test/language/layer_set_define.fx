#ifndef BUFFER_COLOR_SPACE
#define BUFFER_COLOR_SPACE 1
#endif
#ifndef BUFFER_COLOR_BIT_DEPTH
#define BUFFER_COLOR_BIT_DEPTH 8
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
#define RESHADE_DEPTH_INPUT_IS_REVERSED 1
#endif
#ifndef USER_STRENGTH
#define USER_STRENGTH 3
#endif

float4 LayerSetDefineVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 LayerSetDefinePS(float4 pos : SV_Position) : SV_Target
{
    return float4(BUFFER_COLOR_SPACE * 0.1, BUFFER_COLOR_BIT_DEPTH * 0.01, RESHADE_DEPTH_INPUT_IS_REVERSED, USER_STRENGTH * 0.1);
}

technique LayerSetDefine
{
    pass
    {
        VertexShader = LayerSetDefineVS;
        PixelShader  = LayerSetDefinePS;
    }
}
