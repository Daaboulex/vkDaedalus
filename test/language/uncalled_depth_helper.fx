texture UncalledDepthTex : DEPTH;
sampler UncalledDepthSampler { Texture = UncalledDepthTex; };

float ReadDepthNobodyCalls(float2 uv)
{
    return tex2D(UncalledDepthSampler, uv).x;
}

float4 UncalledDepthHelperVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 UncalledDepthHelperPS(float4 pos : SV_Position) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}

technique UncalledDepthHelper
{
    pass
    {
        VertexShader = UncalledDepthHelperVS;
        PixelShader  = UncalledDepthHelperPS;
    }
}
