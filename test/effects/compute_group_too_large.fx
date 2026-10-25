texture2D GroupTex { Width = 64; Height = 1; Format = RGBA8; };
sampler2D GroupSampler { Texture = GroupTex; };
storage2D GroupStorage { Texture = GroupTex; };

void LimitsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void GroupCS(uint3 tid : SV_DispatchThreadID)
{
    tex2Dstore(GroupStorage, int2(tid.x % 64, 0), float4(1.0, 0.0, 0.0, 1.0));
}

float4 GroupPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(GroupSampler, uv);
}

technique ComputeGroupTooLarge
{
    pass
    {
        ComputeShader = GroupCS<4096, 1>;
        DispatchSizeX = 1;
        DispatchSizeY = 1;
    }
    pass
    {
        VertexShader = LimitsVS;
        PixelShader  = GroupPS;
    }
}
