float4 NoTechniqueVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 NoTechniquePS(float4 pos : SV_Position) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
