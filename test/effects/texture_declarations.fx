texture2D DeclBackBufferTex : COLOR;
sampler2D DeclBackBuffer { Texture = DeclBackBufferTex; };

texture2D DeclPacked { Width = 8; Height = 8; Format = RG11B10F; };
sampler2D DeclPackedSampler { Texture = DeclPacked; };

texture2D DeclWide { Width = 8; Height = 8; Format = RGB10A2; };
storage2D DeclWideStorage { Texture = DeclWide; };
sampler2D DeclWideSampler { Texture = DeclWide; };

texture2D DeclMips { Width = 64; Height = 64; Format = RGBA8; MipLevels = 4; };
sampler2D DeclMipsSampler { Texture = DeclMips; MipLODBias = 100; };

texture2D DeclTooManyMips { Width = 16; Height = 16; Format = RGBA8; MipLevels = 16; };
sampler2D DeclTooManyMipsSampler { Texture = DeclTooManyMips; };

texture2D DeclSignedInts { Width = 4; Height = 4; Format = RGBA32I; };
texture2D DeclUnsignedInts { Width = 4; Height = 4; Format = RGBA32U; };
sampler2D<int4> DeclSignedIntsSampler { Texture = DeclSignedInts; MinFilter = LINEAR; MagFilter = LINEAR; };

texture2D DeclIntMips { Width = 16; Height = 16; Format = R32I; MipLevels = 4; };
sampler2D<int> DeclIntMipsSampler { Texture = DeclIntMips; MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR; };

void DeclVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 FillPackedPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0.5, 0.25, 0.125, 1.0);
}

void FillWideCS(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(DeclWideStorage, id.xy, float4(0.25, 0.5, 0.75, 1.0));
}

float4 FillMipsPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(DeclBackBuffer, uv);
}

int FillIntMipsPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return 7;
}

float4 CombinePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 extra = tex2D(DeclPackedSampler, uv) + tex2D(DeclWideSampler, uv)
                 + tex2Dlod(DeclMipsSampler, float4(uv, 0.0, 2.0)) + tex2Dlod(DeclTooManyMipsSampler, float4(uv, 0.0, 3.0));
    extra += float(tex2Dlod(DeclIntMipsSampler, float4(uv, 0.0, 1.0))) * 0.001 + float4(tex2D(DeclSignedIntsSampler, uv)) * 0.001;
    return tex2D(DeclBackBuffer, uv) * 0.6 + extra * 0.1;
}

technique TextureDeclarations
{
    pass
    {
        VertexShader = DeclVS;
        PixelShader = FillPackedPS;
        RenderTarget = DeclPacked;
    }
    pass
    {
        ComputeShader = FillWideCS<8, 8>;
        DispatchSizeX = 1;
        DispatchSizeY = 1;
    }
    pass
    {
        VertexShader = DeclVS;
        PixelShader = FillMipsPS;
        RenderTarget = DeclMips;
    }
    pass
    {
        VertexShader = DeclVS;
        PixelShader = FillMipsPS;
        RenderTarget = DeclTooManyMips;
    }
    pass
    {
        VertexShader = DeclVS;
        PixelShader = FillIntMipsPS;
        RenderTarget = DeclIntMips;
    }
    pass
    {
        VertexShader = DeclVS;
        PixelShader = CombinePS;
    }
}
