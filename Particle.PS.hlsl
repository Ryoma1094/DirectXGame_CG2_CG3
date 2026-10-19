#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer gMaterial : register(b0)
{
    Material gMaterialData;
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterialData.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    output.color = gMaterialData.color * textureColor;

    if (output.color.a == 0.0)
    {
        discard;
    }

    return output;
}