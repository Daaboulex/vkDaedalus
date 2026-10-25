uniform int PackInfo < ui_type = "radio"; ui_label = " "; ui_text = "Pack UI fixture: the annotations shader packs use."; > = 0;
uniform int PQuality < ui_type = "radio"; ui_label = "P Quality"; ui_items = "Coarse\0Fine\0Finest\0"; ui_category = "Look"; > = 1;
uniform float PRadius < ui_type = "slider"; ui_label = "P Radius"; ui_min = 0.0; ui_max = 10.0; ui_units = " m"; ui_category = "Look"; > = 2.5;
uniform float3 PColour < ui_type = "color"; ui_label = "P Colour"; ui_category = "Look"; > = float3(0.2, 0.4, 0.6);
uniform float PAdvanced < ui_type = "slider"; ui_label = "P Advanced"; ui_category = "Advanced"; ui_category_closed = true; > = 0.5;
uniform float PHidden < hidden = true; ui_label = "P Hidden"; > = 0.25;

void PackUiVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PackUiPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float sum = PackInfo + PQuality + PRadius + PColour.r + PColour.g + PColour.b + PAdvanced + PHidden;
    return float4(frac(sum * 0.01), 0.5, 0.75, 1.0);
}

technique PackUi
{
    pass
    {
        VertexShader = PackUiVS;
        PixelShader = PackUiPS;
    }
}
