texture2D ManyTex { Width = 64; Height = 1; Format = RGBA8; };
sampler2D ManySampler { Texture = ManyTex; };
storage2D ManyStorage { Texture = ManyTex; };

void LimitsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void ManyCS(uint3 tid : SV_DispatchThreadID)
{
    tex2Dstore(ManyStorage, int2(tid.x % 64, 0), float4(0.0, 1.0, 0.0, 1.0));
}

float4 ManyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(ManySampler, uv);
}

technique DispatchTooMany
{
    pass
    {
        ComputeShader = ManyCS<1, 1>;
        DispatchSizeX = 1;
        DispatchSizeY = 2147483647;
    }
    pass
    {
        VertexShader = LimitsVS;
        PixelShader  = ManyPS;
    }
}
