texture2D InvertInputColorTex : COLOR;
sampler2D InvertInputColor { Texture = InvertInputColorTex; };

void InvertInputVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 InvertInputPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(1.0 - tex2D(InvertInputColor, uv).rgb, 1.0);
}

technique InvertInput
{
    pass
    {
        VertexShader = InvertInputVS;
        PixelShader = InvertInputPS;
    }
}
