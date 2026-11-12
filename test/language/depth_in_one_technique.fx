texture2D DepthInOneTex : DEPTH;
sampler2D DepthInOne { Texture = DepthInOneTex; };

void DepthInOneVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PlainPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(uv, 0.0, 1.0);
}

float4 ReadsDepthPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float depth = tex2D(DepthInOne, uv).r;
    return float4(depth, depth, depth, 1.0);
}

technique Plain
{
    pass
    {
        VertexShader = DepthInOneVS;
        PixelShader = PlainPS;
    }
}

technique ReadsDepth
{
    pass
    {
        VertexShader = DepthInOneVS;
        PixelShader = ReadsDepthPS;
    }
}
