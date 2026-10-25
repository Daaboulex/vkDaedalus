uniform float UniformTimer < source = "timer"; >;
uniform int UniformFrame < source = "framecount"; >;

texture2D UniformBackBufferTex : COLOR;
sampler2D UniformBackBuffer { Texture = UniformBackBufferTex; };

float4 UniformVS(uint id : SV_VertexID, out float2 uv : TEXCOORD) : SV_Position
{
    uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 UniformPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 color = tex2D(UniformBackBuffer, uv);
    float pulse = 0.5 + 0.5 * sin(UniformTimer * 0.001 + float(UniformFrame % 60));
    return float4(color.rgb * lerp(0.98, 1.0, pulse), color.a);
}

technique UniformSmoke
{
    pass
    {
        VertexShader = UniformVS;
        PixelShader  = UniformPS;
    }
}
