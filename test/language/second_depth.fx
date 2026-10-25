texture2D FirstDepthTex : DEPTH;
texture2D SecondDepthTex : DEPTH;
sampler2D SecondDepthSampler { Texture = SecondDepthTex; };

void SecondDepthVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 SecondDepthPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float depth = tex2D(SecondDepthSampler, uv).r;
    return float4(depth, depth, depth, 1.0);
}

technique SecondDepth
{
    pass
    {
        VertexShader = SecondDepthVS;
        PixelShader = SecondDepthPS;
    }
}
