texture2D TypoTex { Width = 4; Height = 4; Format = RGBA8; };
storage2D TypoStorage { Texture = TypoTex; };

void TypoCS(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(TypoStorage, id.xy);
}

technique Typo
{
    pass
    {
        ComputeShader = TypoCS<4, 4>;
        DispatchSizeX = 1;
        DispatchSizeY = 1;
    }
}
