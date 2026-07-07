#include "Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b2);

struct PixelShaderOutput{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input){
    PixelShaderOutput output;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord).bgra;

    if (gMaterial.enableLighting != 0){
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);

        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);

        output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
    
    }else{
        output.color = gMaterial.color * textureColor;
    }

    return output;
}