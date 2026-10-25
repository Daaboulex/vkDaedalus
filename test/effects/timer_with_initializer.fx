uniform float TimerWithInitializer < source = "timer"; > = 0.0;

void TimerVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 TimerColourPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(saturate(TimerWithInitializer * 0.00005), 0.0, 0.0, 1.0);
}

technique TimerWithInitializerSmoke
{
    pass
    {
        VertexShader = TimerVS;
        PixelShader = TimerColourPS;
    }
}
