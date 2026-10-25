texture2D LeftAloneTex { Width = 64; Height = 64; Format = RGBA8; MipLevels = 4; };
sampler2D LeftAlone { Texture = LeftAloneTex; };

void LeftAloneVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PaintLevelZeroPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}

float4 ShowLevelTwoPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2Dlod(LeftAlone, float4(uv, 0.0, 2.0));
}

technique MipsLeftAlone
{
    pass
    {
        VertexShader = LeftAloneVS;
        PixelShader = PaintLevelZeroPS;
        RenderTarget = LeftAloneTex;
        GenerateMipMaps = false;
    }
    pass
    {
        VertexShader = LeftAloneVS;
        PixelShader = ShowLevelTwoPS;
    }
}
