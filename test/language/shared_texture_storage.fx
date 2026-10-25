namespace SharedTextureProbe
{
    texture2D PublishedColor
    {
        Width = BUFFER_WIDTH;
        Height = BUFFER_HEIGHT;
        Format = RGBA8;
    };

    storage2D PublishedColorStorage { Texture = PublishedColor; };
}

void WriteSharedColorCS(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(SharedTextureProbe::PublishedColorStorage, id.xy, float4(0.75, 0.5, 0.25, 1.0));
}

technique SharedTextureStorage
{
    pass
    {
        ComputeShader = WriteSharedColorCS<8, 8>;
        DispatchSizeX = BUFFER_WIDTH / 8;
        DispatchSizeY = BUFFER_HEIGHT / 8;
    }
}
