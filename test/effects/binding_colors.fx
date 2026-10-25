texture2D BindRedTex { Width = 16; Height = 16; Format = RGBA8; };
texture2D BindGreenTex { Width = 16; Height = 16; Format = RGBA8; };
texture2D BindBlueTex { Width = 16; Height = 16; Format = RGBA8; };

sampler2D BindRed { Texture = BindRedTex; };
sampler2D BindGreen { Texture = BindGreenTex; };
sampler2D BindBlue { Texture = BindBlueTex; };

void BindVS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 RedPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
float4 GreenPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }
float4 BluePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return float4(0.0, 0.0, 1.0, 1.0); }

float4 SkipGreenPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(BindBlue, uv) + 0.0 * tex2D(BindRed, uv);
}

technique BindingColors
{
    pass { VertexShader = BindVS; PixelShader = RedPS; RenderTarget = BindRedTex; }
    pass { VertexShader = BindVS; PixelShader = GreenPS; RenderTarget = BindGreenTex; }
    pass { VertexShader = BindVS; PixelShader = BluePS; RenderTarget = BindBlueTex; }
    pass { VertexShader = BindVS; PixelShader = SkipGreenPS; }
}
