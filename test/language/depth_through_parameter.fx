texture2D ParameterDepthTex : DEPTH;
sampler2D ParameterDepthSampler { Texture = ParameterDepthTex; };

float4 ReadThrough(sampler2D source, float2 uv)
{
    return tex2D(source, uv);
}

void ParameterDepthVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 ParameterDepthPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float depth = ReadThrough(ParameterDepthSampler, uv).r;
    return float4(depth, depth, depth, 1.0);
}

technique ParameterDepth
{
    pass
    {
        VertexShader = ParameterDepthVS;
        PixelShader = ParameterDepthPS;
    }
}
