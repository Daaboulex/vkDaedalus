#define MESSAGE_FROM_A_MACRO _Pragma("message \"the pragma operator works inside a macro\"")
MESSAGE_FROM_A_MACRO

float4 PragmaVS(uint id : SV_VertexID) : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 PragmaPS(float4 pos : SV_Position) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}

technique PragmaOperator
{
    pass
    {
        VertexShader = PragmaVS;
        PixelShader  = PragmaPS;
    }
}
