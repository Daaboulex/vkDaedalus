texture2D UnknownFormatTex { Width = 4; Height = 4; Format = 5; };
sampler2D UnknownFormatSampler { Texture = UnknownFormatTex; };

void UnknownFormatVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 UnknownFormatPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(UnknownFormatSampler, uv);
}

technique TextureUnknownFormat
{
    pass
    {
        VertexShader = UnknownFormatVS;
        PixelShader = UnknownFormatPS;
    }
}
