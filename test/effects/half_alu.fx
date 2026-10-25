texture2D HalfAluColorTex : COLOR;
sampler2D HalfAluColor { Texture = HalfAluColorTex; };

void HalfAluVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 HalfAluPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    min16float3 color = min16float3(tex2D(HalfAluColor, uv).rgb);
    [loop]
    for (int i = 0; i < 256; i++)
        color = color * min16float(0.999) + min16float3(0.0005, 0.0004, 0.0003);
    return float4(color, 1.0);
}

technique HalfAlu
{
    pass
    {
        VertexShader = HalfAluVS;
        PixelShader = HalfAluPS;
    }
}
