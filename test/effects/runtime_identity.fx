#if !defined(BUFFER_COLOR_FORMAT) || BUFFER_COLOR_FORMAT == 0
#error "the back buffer's format is not named"
#endif
#if !defined(__VENDOR__) || !defined(__DEVICE__)
#error "the GPU is not named"
#endif
#if BUFFER_WIDTH != 1920 && __VENDOR__ == 0
#error "the chain's compile for the game's swapchain does not name the GPU's vendor"
#endif
#if !defined(__RENDERER__) || __RENDERER__ < 0x21100
#error "the renderer is not Vulkan 1.1 or later"
#endif
#if !defined(__APPLICATION__) || __APPLICATION__ == 0
#error "the program is not named"
#endif
#if !defined(__RESHADE_PERMUTATION__) || __RESHADE_PERMUTATION__ != 0
#error "this is not the back buffer's permutation"
#endif

texture2D IdentityBackBufferTex : COLOR;
sampler2D IdentityBackBuffer { Texture = IdentityBackBufferTex; };
texture2D IdentityCopyTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = BUFFER_COLOR_FORMAT; };
sampler2D IdentityCopy { Texture = IdentityCopyTex; };

void IdentityVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 IdentityCopyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(IdentityBackBuffer, uv);
}

float4 IdentityOutputPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(IdentityCopy, uv);
}

technique RuntimeIdentity
{
    pass
    {
        VertexShader = IdentityVS;
        PixelShader = IdentityCopyPS;
        RenderTarget = IdentityCopyTex;
    }
    pass
    {
        VertexShader = IdentityVS;
        PixelShader = IdentityOutputPS;
    }
}
