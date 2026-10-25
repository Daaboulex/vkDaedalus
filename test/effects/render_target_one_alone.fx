texture2D SideTex { Width = 64; Height = 64; Format = RGBA8; };

void TargetOneVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 BackBufferGreenPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}

technique TargetOneAlone
{
    pass
    {
        VertexShader = TargetOneVS;
        PixelShader = BackBufferGreenPS;
        RenderTarget1 = SideTex;
    }
}
