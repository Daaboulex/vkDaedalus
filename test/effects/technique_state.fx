uniform int   StateRandom < source = "random"; min = 1; max = 30000; >;
uniform float StateFrames < source = "framecount"; >;

texture2D StateRememberedTex { Width = 1; Height = 1; Format = R32F; };
sampler2D StateRemembered { Texture = StateRememberedTex; MinFilter = POINT; MagFilter = POINT; };

void StateVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 RememberPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(float(StateRandom), 0.0, 0.0, 1.0);
}

float4 ComparePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const bool sameRandom   = tex2Dfetch(StateRemembered, int2(0, 0)).r == float(StateRandom);
    const bool framesCounted = StateFrames > 100.0 && StateFrames < 100000.0;
    return sameRandom && framesCounted ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
}

technique Remember
{
    pass
    {
        VertexShader = StateVS;
        PixelShader = RememberPS;
        RenderTarget = StateRememberedTex;
    }
}

technique Compare
{
    pass
    {
        VertexShader = StateVS;
        PixelShader = ComparePS;
    }
}
