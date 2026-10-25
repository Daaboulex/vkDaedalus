uniform bool PBool < ui_label = "P Bool"; > = false;
uniform int PInt < ui_type = "slider"; ui_label = "P Int"; ui_min = 0; ui_max = 10; > = 1;
uniform int PCombo < ui_type = "combo"; ui_label = "P Combo"; ui_items = "Zero\0One\0Two\0"; > = 0;
uniform uint PUint < ui_type = "slider"; ui_label = "P Uint"; ui_min = 0; ui_max = 10; > = 1;
uniform float2 PFloat2 < ui_type = "slider"; ui_label = "P Float2"; ui_min = 0.0; ui_max = 1.0; > = float2(0.1, 0.2);
uniform int2 PInt2 < ui_type = "slider"; ui_label = "P Int2"; ui_min = 0; ui_max = 10; > = int2(1, 2);
uniform uint2 PUint2 < ui_type = "slider"; ui_label = "P Uint2"; ui_min = 0; ui_max = 10; > = uint2(1, 2);

void EveryParamVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 EveryParamPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float sum = (PBool ? 1.0 : 0.0) + PInt + PCombo + PUint + PFloat2.x + PFloat2.y + PInt2.x + PInt2.y + PUint2.x + PUint2.y;
    return float4(frac(sum * 0.01), 0.5, 0.75, 1.0);
}

technique EveryParamType
{
    pass
    {
        VertexShader = EveryParamVS;
        PixelShader = EveryParamPS;
    }
}
