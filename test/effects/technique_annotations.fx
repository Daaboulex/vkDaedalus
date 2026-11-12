texture2D TechniqueAnnotationsBackBufferTex : COLOR;
sampler2D TechniqueAnnotationsBackBuffer { Texture = TechniqueAnnotationsBackBufferTex; };

void TechniqueAnnotationsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 KeepPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(TechniqueAnnotationsBackBuffer, uv);
}

technique Base < ui_label = "Base pass"; ui_tooltip = "The technique a bare row runs"; >
{
    pass
    {
        VertexShader = TechniqueAnnotationsVS;
        PixelShader = KeepPS;
    }
}

technique Keyed < ui_label = "Keyed pass"; >
{
    pass
    {
        VertexShader = TechniqueAnnotationsVS;
        PixelShader = KeepPS;
    }
}

technique AlwaysOn < enabled = true; >
{
    pass
    {
        VertexShader = TechniqueAnnotationsVS;
        PixelShader = KeepPS;
    }
}

technique Brief < timeout = 300; >
{
    pass
    {
        VertexShader = TechniqueAnnotationsVS;
        PixelShader = KeepPS;
    }
}

technique Hidden < hidden = true; >
{
    pass
    {
        VertexShader = TechniqueAnnotationsVS;
        PixelShader = KeepPS;
    }
}
