texture2D CRBackBufferTex : COLOR;
sampler2D CRBackBuffer { Texture = CRBackBufferTex; };

texture2D CRCopyTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
storage2D CRCopyStorage { Texture = CRCopyTex; };
sampler2D CRCopy { Texture = CRCopyTex; };

void CRVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PaintRedPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}

void CopyBackBufferCS(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(CRCopyStorage, id.xy, tex2Dfetch(CRBackBuffer, id.xy));
}

float4 ShowCopyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(CRCopy, uv);
}

technique ComputeReadsBackBuffer
{
    pass
    {
        VertexShader = CRVS;
        PixelShader = PaintRedPS;
    }
    pass
    {
        ComputeShader = CopyBackBufferCS<8, 8>;
        DispatchSizeX = BUFFER_WIDTH / 8;
        DispatchSizeY = BUFFER_HEIGHT / 8;
    }
    pass
    {
        VertexShader = CRVS;
        PixelShader = ShowCopyPS;
    }
}
