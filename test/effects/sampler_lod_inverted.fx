texture2D InvertedTex { Width = 16; Height = 16; MipLevels = 5; Format = RGBA8; };
sampler2D InvertedSampler { Texture = InvertedTex; MinLOD = 4; MaxLOD = 1; };

void LimitsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 InvertedPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(InvertedSampler, uv);
}

technique SamplerLodInverted
{
    pass
    {
        VertexShader = LimitsVS;
        PixelShader  = InvertedPS;
    }
}
