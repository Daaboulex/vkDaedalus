texture2D DimensionsBackBufferTex : COLOR;
sampler2D DimensionsBackBuffer { Texture = DimensionsBackBufferTex; };

texture1D DimensionsLine { Width = 8; Format = RGBA8; };
sampler1D DimensionsLineSampler { Texture = DimensionsLine; };
storage1D DimensionsLineStorage { Texture = DimensionsLine; };

texture3D DimensionsFlat { Width = 4; Height = 4; Format = RGBA8; };
sampler3D DimensionsFlatSampler { Texture = DimensionsFlat; };

texture3D DimensionsVolume { Width = 4; Height = 4; Depth = 4; Format = RGBA8; };
sampler3D DimensionsVolumeSampler { Texture = DimensionsVolume; };
storage3D DimensionsVolumeStorage { Texture = DimensionsVolume; };

texture3D DimensionsVolumeFile < source = "solid.png"; > { Width = 4; Height = 4; Depth = 4; Format = RGBA8; };
sampler3D DimensionsVolumeFileSampler { Texture = DimensionsVolumeFile; };

void DimensionsVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void DimensionsCS(uint3 tid : SV_DispatchThreadID)
{
    tex3Dstore(DimensionsVolumeStorage, int3(tid), float4(tid / 4.0, 1.0));
    if (tid.y == 0 && tid.z == 0)
        tex1Dstore(DimensionsLineStorage, int(tid.x), float4(0.5, 0.25, 0.125, 1.0));
}

float4 DimensionsPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 colour = tex2D(DimensionsBackBuffer, uv);
    colour += 0.1 * tex1D(DimensionsLineSampler, uv.x);
    colour += 0.1 * tex3D(DimensionsFlatSampler, float3(uv, 0.5));
    colour += 0.1 * tex3D(DimensionsVolumeSampler, float3(uv, 0.5));
    colour += 0.1 * tex3D(DimensionsVolumeFileSampler, float3(uv, 0.5));
    return colour;
}

technique TextureDimensions
{
    pass
    {
        ComputeShader = DimensionsCS<4, 4, 4>;
        DispatchSizeX = 1;
        DispatchSizeY = 1;
        DispatchSizeZ = 1;
    }
    pass
    {
        VertexShader = DimensionsVS;
        PixelShader  = DimensionsPS;
    }
}
