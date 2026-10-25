texture2D TwoPassBackBufferTex : COLOR;
sampler2D TwoPassBackBuffer { Texture = TwoPassBackBufferTex; };

texture2D TwoPassHalf { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = RGBA8; };
sampler2D TwoPassHalfSampler { Texture = TwoPassHalf; };

void TwoPassVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 DownsamplePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(TwoPassBackBuffer, uv);
}

float4 UpsamplePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return lerp(tex2D(TwoPassBackBuffer, uv), tex2D(TwoPassHalfSampler, uv), 0.5);
}

technique TwoPassSmoke
{
    pass
    {
        VertexShader = TwoPassVS;
        PixelShader = DownsamplePS;
        RenderTarget = TwoPassHalf;
    }
    pass
    {
        VertexShader = TwoPassVS;
        PixelShader = UpsamplePS;
    }
}
