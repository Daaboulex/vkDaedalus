texture2D TooWideTex { Width = 65536; Height = 4; Format = RGBA8; };
sampler2D TooWideSampler { Texture = TooWideTex; };

void LimitsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 TooWidePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(TooWideSampler, uv);
}

technique TextureTooWide
{
    pass
    {
        VertexShader = LimitsVS;
        PixelShader  = TooWidePS;
    }
}
