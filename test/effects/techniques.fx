texture2D TechniquesBackBufferTex : COLOR;
sampler2D TechniquesBackBuffer { Texture = TechniquesBackBufferTex; };

texture2D TechniquesSavedTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler2D TechniquesSaved { Texture = TechniquesSavedTex; };

void TechniquesVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 SavePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(TechniquesBackBuffer, uv);
}

float4 RestorePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(TechniquesSaved, uv);
}

float4 TintPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}

technique Save
{
    pass
    {
        VertexShader = TechniquesVS;
        PixelShader = SavePS;
        RenderTarget = TechniquesSavedTex;
    }
}

technique Restore
{
    pass
    {
        VertexShader = TechniquesVS;
        PixelShader = RestorePS;
    }
}

technique Tint
{
    pass
    {
        VertexShader = TechniquesVS;
        PixelShader = TintPS;
    }
}
