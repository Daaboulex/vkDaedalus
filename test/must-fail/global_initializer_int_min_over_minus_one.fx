static const int Unfoldable = (-2147483647 - 1) / -1;

float4 GlobalInitializerVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 GlobalInitializerPS(float4 pos : SV_Position) : SV_Target
{
    return float4(Unfoldable, 0.0, 0.0, 1.0);
}

technique GlobalInitializer
{
    pass
    {
        VertexShader = GlobalInitializerVS;
        PixelShader  = GlobalInitializerPS;
    }
}
