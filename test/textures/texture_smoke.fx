texture ResizedTex < source = "solid.png"; >
{
    Width  = 16;
    Height = 16;
    Format = RGBA8;
};

texture RedGreenTex < source = "solid.png"; >
{
    Width  = 8;
    Height = 8;
    Format = RG8;
};

texture DdsTex < source = "solid.dds"; >
{
    Width  = 4;
    Height = 4;
    Format = RGBA8;
};

sampler ResizedSampler { Texture = ResizedTex; };
sampler RedGreenSampler { Texture = RedGreenTex; };
sampler DdsSampler { Texture = DdsTex; };

float4 TextureSmokeVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 TextureSmokePS(float4 pos : SV_Position) : SV_Target
{
    return tex2D(ResizedSampler, float2(0.5, 0.5)) + tex2D(RedGreenSampler, float2(0.5, 0.5)) + tex2D(DdsSampler, float2(0.5, 0.5));
}

technique TextureSmoke
{
    pass
    {
        VertexShader = TextureSmokeVS;
        PixelShader  = TextureSmokePS;
    }
}
