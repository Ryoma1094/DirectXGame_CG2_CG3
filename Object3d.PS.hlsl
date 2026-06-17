#include "Object3d.hlsli"
struct Material{
    float4 color;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


float4 main(VertexShaderOutput input) : SV_TARGET{
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    float4 outputColor = gMaterial.color * textureColor;
    return outputColor;
}