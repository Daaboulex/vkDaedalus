float4 IntMinFoldVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 IntMinFoldPS(float4 pos : SV_Position) : SV_Target
{
    const int minOverMinusOne = (-2147483647 - 1) / -1;
    const int minModMinusOne = (-2147483647 - 1) % -1;
    return float4(minOverMinusOne, minModMinusOne, 0.0, 1.0);
}

technique IntMinFold
{
    pass
    {
        VertexShader = IntMinFoldVS;
        PixelShader  = IntMinFoldPS;
    }
}
