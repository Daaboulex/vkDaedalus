texture2D GatherBackBufferTex : COLOR;
sampler2D GatherBackBuffer { Texture = GatherBackBufferTex; };

float4 GatherVS(uint id : SV_VertexID, out float2 uv : TEXCOORD) : SV_Position
{
    uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 GatherPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 green = tex2Dgather(GatherBackBuffer, uv, 1);
    float4 alpha = tex2Dgatheroffset(GatherBackBuffer, uv, int2(1, 0), 3);
    float4 red   = tex2Dgather0(GatherBackBuffer, uv);
    float4 blue  = tex2Dgather2(GatherBackBuffer, uv);
    return (green + alpha + red + blue) * 0.25;
}

technique GatherCompat
{
    pass
    {
        VertexShader = GatherVS;
        PixelShader  = GatherPS;
    }
}
