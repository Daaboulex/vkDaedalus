texture2D BindingA { Width = 4; Height = 4; Format = RGBA8; };
texture2D BindingB { Width = 4; Height = 4; Format = RGBA8; };
texture2D BindingC { Width = 4; Height = 4; Format = RGBA8; };

sampler2D SamplerA { Texture = BindingA; };
sampler2D SamplerB { Texture = BindingB; };
sampler2D SamplerC { Texture = BindingC; };

storage2D StorageA { Texture = BindingA; };
storage2D StorageB { Texture = BindingB; };

void BindingVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 SkipsSamplerBPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(SamplerA, uv) + tex2D(SamplerC, uv);
}

void WritesStorageBCS(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(StorageB, id.xy, float4(1.0, 0.0, 0.0, 1.0));
}

technique BindingOrder
{
    pass
    {
        ComputeShader = WritesStorageBCS<4, 4>;
        DispatchSizeX = 1;
        DispatchSizeY = 1;
    }
    pass
    {
        VertexShader = BindingVS;
        PixelShader = SkipsSamplerBPS;
    }
}
