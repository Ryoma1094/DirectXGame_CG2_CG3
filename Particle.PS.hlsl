#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    float alpha = (1.0f - textureColor.r) * textureColor.a;

    if (alpha <= 0.05f)
    {
        discard;
    }

    output.color.rgb = gMaterial.color.rgb * input.color.rgb;
    output.color.a = gMaterial.color.a * input.color.a * alpha;

    return output;
}