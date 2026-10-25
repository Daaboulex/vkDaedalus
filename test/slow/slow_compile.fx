#define TEN(x) x x x x x x x x x x
#define HUNDRED(x) TEN(TEN(x))
#define TEN_THOUSAND(x) HUNDRED(HUNDRED(x))
#define SLOW(x) TEN_THOUSAND(HUNDRED(x x x x x))

void SlowCompileVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 SlowCompilePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float sum = uv.x;
    SLOW({ })
    return float4(frac(sum), 0.5, 0.75, 1.0);
}

technique SlowCompile
{
    pass
    {
        VertexShader = SlowCompileVS;
        PixelShader = SlowCompilePS;
    }
}
