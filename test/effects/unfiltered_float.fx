texture2D UnfilteredTex { Width = 64; Height = 64; Format = R32F; MipLevels = 4; };
sampler2D UnfilteredSampler { Texture = UnfilteredTex; };

void UnfilteredVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float FillUnfilteredPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return uv.x;
}

float4 ReadUnfilteredPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(UnfilteredSampler, uv).xxxx;
}

technique UnfilteredFloat
{
    pass
    {
        VertexShader = UnfilteredVS;
        PixelShader  = FillUnfilteredPS;
        RenderTarget = UnfilteredTex;
    }
    pass
    {
        VertexShader = UnfilteredVS;
        PixelShader  = ReadUnfilteredPS;
    }
}
