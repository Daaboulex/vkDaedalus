uniform bool OverlayOpen < source = "overlay_open"; >;

void UnknownSourceVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 UnknownSourcePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return OverlayOpen ? float4(1.0, 1.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}

technique UnknownSource
{
    pass
    {
        VertexShader = UnknownSourceVS;
        PixelShader = UnknownSourcePS;
    }
}
